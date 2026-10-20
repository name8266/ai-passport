#include "brake_sound.h"
#include "bsp_audio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <stdatomic.h>

typedef struct { bh_sound_t sound; unsigned generation; } sound_job_t;
static QueueHandle_t normal_queue, urgent_queue;
static atomic_uint generation;
static atomic_uint max_feed_gap;
uint32_t bh_sound_max_gap_us(void) {return atomic_load(&max_feed_gap);}
static const char *TAG="brake_audio";
void bh_sound_new_round(void) {
    atomic_fetch_add(&generation,1);
    if(normal_queue)xQueueReset(normal_queue);
    if(urgent_queue)xQueueReset(urgent_queue);
}
void bh_sound_post(bh_sound_t sound,bool urgent) {
    if(!normal_queue||sound>=SND_COUNT)return;
    sound_job_t job={sound,atomic_load(&generation)};
    if(urgent) {
        // A fault/result makes queued low-priority warnings stale.
        xQueueReset(normal_queue);
        (void)xQueueOverwrite(urgent_queue,&job);
    }
    else (void)xQueueSend(normal_queue,&job,0);
}
static void worker(void *arg) {
    (void)arg;
    if(bsp_audio_init()!=ESP_OK || bsp_audio_set_format(16000,16,1)!=ESP_OK) {
        ESP_LOGE(TAG,"Codec unavailable; game continues silently");vTaskDelete(NULL);return;
    }
    bsp_audio_set_volume(70);
    sound_job_t job;
    bool have=false;
    int16_t pcm[320];
    for(;;) {
        if(!have && xQueueReceive(urgent_queue,&job,0)!=pdTRUE &&
           xQueueReceive(normal_queue,&job,pdMS_TO_TICKS(10))!=pdTRUE)continue;
        have=false;
        const bh_clip_t *clip=&bh_clips[job.sound];
        int64_t last_feed=0;
        for(uint32_t pos=0;pos<clip->count;) {
            if(job.generation!=atomic_load(&generation))break;
            sound_job_t priority;
            if(xQueueReceive(urgent_queue,&priority,0)==pdTRUE) {job=priority;have=true;break;}
            size_t count=clip->count-pos;if(count>320)count=320;
            for(size_t i=0;i<count;++i)pcm[i]=(int16_t)((int8_t)clip->samples[pos+i]*256);
            int64_t now=esp_timer_get_time();
            if(last_feed) {
                unsigned gap=(unsigned)(now-last_feed);
                if(gap>atomic_load(&max_feed_gap))atomic_store(&max_feed_gap,gap);
            }
            last_feed=now;
            if(bsp_audio_write(pcm,count*sizeof(pcm[0]))!=ESP_OK) {
                ESP_LOGW(TAG,"PCM write failed; dropping clip");break;
            }
            pos+=(uint32_t)count;
        }
    }
}
bool bh_sound_init(void) {
    normal_queue=xQueueCreate(6,sizeof(sound_job_t));urgent_queue=xQueueCreate(1,sizeof(sound_job_t));
    if(normal_queue&&urgent_queue && xTaskCreate(worker,"brake_audio",4096,NULL,5,NULL)==pdPASS)return true;
    if(normal_queue)vQueueDelete(normal_queue);
    if(urgent_queue)vQueueDelete(urgent_queue);
    normal_queue=urgent_queue=NULL;return false;
}
