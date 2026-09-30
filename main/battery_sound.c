#include "battery_sound.h"
#include "bsp_audio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include <stdint.h>
#include <stdatomic.h>

typedef struct { bool reward;uint8_t volume; } sound_t;
static QueueHandle_t s_queue;
static atomic_bool s_available=true;
static void worker(void *arg) {
    (void)arg;bool initialized=false,sleeping=false;sound_t sound;
    for(;;) {
        if(xQueueReceive(s_queue,&sound,portMAX_DELAY)!=pdTRUE)continue;
        esp_err_t err=ESP_OK;
        if(!initialized) { err=bsp_audio_init();initialized=err==ESP_OK; }
        else if(sleeping)err=bsp_audio_wake();
        if(err==ESP_OK)err=bsp_audio_set_format(16000,16,1);
        if(err==ESP_OK)bsp_audio_set_volume(sound.volume>60 ? 60 : sound.volume);
        /* Small synthesized rising chirps. No full PCM buffer, no UI lock. */
        for(unsigned note=0;note<3 && err==ESP_OK;++note) {
            unsigned frequency=(sound.reward ? 660 : 440)+note*110;
            uint32_t phase=0;int16_t pcm[128];
            for(unsigned block=0;block<20 && err==ESP_OK;++block) {
                for(unsigned i=0;i<128;++i) {
                    phase+=frequency*65536u/16000u;
                    int32_t wave=(int32_t)(phase&0xffffu);wave=wave<32768 ? wave : 65535-wave;
                    int32_t envelope=(int32_t)(block<10 ? block+1 : 20-block);
                    pcm[i]=(int16_t)((wave-16384)*envelope/100);
                }
                err=bsp_audio_write(pcm,sizeof(pcm));
            }
        }
        if(initialized) { if(bsp_audio_sleep()!=ESP_OK)err=ESP_FAIL;sleeping=true; }
        atomic_store(&s_available,err==ESP_OK);
    }
}
bool battery_sound_init(void) {
    s_queue=xQueueCreate(2,sizeof(sound_t));if(!s_queue){atomic_store(&s_available,false);return false;}
    if(xTaskCreate(worker,"care_sound",4096,NULL,4,NULL)!=pdPASS){vQueueDelete(s_queue);s_queue=NULL;atomic_store(&s_available,false);return false;}
    return true;
}
void battery_sound_notify(bool reward,unsigned volume) {
    if(!s_queue || !volume)return;
    sound_t sound={reward,(uint8_t)volume};(void)xQueueSend(s_queue,&sound,0);
}
bool battery_sound_available(void) { return atomic_load(&s_available); }
