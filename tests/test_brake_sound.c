#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../main/brake_sound.c"
struct fake_queue { unsigned count,capacity,item; unsigned char data[8][32]; };
static TaskFunction_t task_entry;
static unsigned allocations,fail_create,creates,task_fail,writes,mode,long_pos;
static jmp_buf done;
int64_t esp_timer_get_time(void) { static int64_t clock;return clock+=20000; }
static uint8_t sample_start[640],sample_break[320],sample_win[320];
static uint8_t sample_long[1003];
const bh_clip_t bh_clips[SND_COUNT]={
    [SND_START]={sample_start,640},[SND_BREAK]={sample_break,320},[SND_WIN]={sample_win,320},[SND_100]={sample_long,1003}
};
QueueHandle_t xQueueCreate(unsigned count,unsigned size) {
    if(++creates==fail_create)return NULL;
    struct fake_queue *q=calloc(1,sizeof(*q));assert(q && count<=8 && size<=32);
    q->capacity=count;q->item=size;++allocations;return q;
}
void vQueueDelete(QueueHandle_t q) {assert(q);free(q);--allocations;}
int xQueueSend(QueueHandle_t q,const void *data,unsigned wait) {
    assert(!wait);if(q->count==q->capacity)return 0;
    memcpy(q->data[q->count++],data,q->item);return pdTRUE;
}
int xQueueOverwrite(QueueHandle_t q,const void *data) {assert(q->capacity==1);memcpy(q->data[0],data,q->item);q->count=1;return pdTRUE;}
int xQueueReceive(QueueHandle_t q,void *data,unsigned wait) {
    (void)wait;if(!q->count)return 0;
    memcpy(data,q->data[0],q->item);--q->count;
    for(unsigned i=0;i<q->count;++i)memcpy(q->data[i],q->data[i+1],q->item);
    return pdTRUE;
}
void xQueueReset(QueueHandle_t q) {q->count=0;}
int xTaskCreate(TaskFunction_t fn,const char *name,unsigned stack,void *arg,unsigned prio,void *task) {
    (void)name;(void)stack;(void)arg;(void)prio;(void)task;task_entry=fn;return !task_fail;
}
void vTaskDelete(void *task) {(void)task;longjmp(done,1);}
esp_err_t bsp_audio_init(void) {return ESP_OK;}
esp_err_t bsp_audio_set_format(uint32_t hz,uint8_t bits,uint8_t ch) {assert(hz==16000&&bits==16&&ch==1);return ESP_OK;}
void bsp_audio_set_volume(uint8_t volume) {assert(volume<=100);}
esp_err_t bsp_audio_write(const void *data,size_t bytes) {
    const int16_t *pcm=data;++writes;
    if(mode==2) {
        unsigned left=1003-long_pos,n=left>320?320:left;assert(bytes==n*2);
        for(unsigned i=0;i<n;++i)assert(pcm[i]==(int8_t)sample_long[long_pos+i]*256);
        long_pos+=n;
        if(long_pos==1003)longjmp(done,1);
        return ESP_OK;
    }
    assert(bytes==640);
    if(writes==1) {
        assert(pcm[0]==256);
        if(mode==0)bh_sound_post(SND_BREAK,true);
        else {bh_sound_new_round();bh_sound_post(SND_WIN,false);}
    } else {
        assert(pcm[0]==(mode==0?1280:1792));longjmp(done,1);
    }
    return ESP_OK;
}
static void cleanup(void) {vQueueDelete(normal_queue);vQueueDelete(urgent_queue);normal_queue=urgent_queue=NULL;assert(!allocations);}
int main(void) {
    for(unsigned i=1;i<=2;++i) {
        creates=0;fail_create=i;assert(!bh_sound_init());assert(!allocations);
    }
    creates=fail_create=0;task_fail=1;assert(!bh_sound_init()&&!allocations);task_fail=0;
    assert(bh_sound_init());
    for(int i=0;i<100;++i)bh_sound_post(SND_START,false);
    assert(normal_queue->count==6);bh_sound_new_round();assert(!normal_queue->count);
    memset(sample_start,1,sizeof(sample_start));memset(sample_break,5,sizeof(sample_break));memset(sample_win,7,sizeof(sample_win));
    for(mode=0;mode<2;++mode) {
        bh_sound_new_round();writes=0;bh_sound_post(SND_START,false);
        if(!setjmp(done))task_entry(NULL);
        assert(writes==2);
    }
    for(unsigned i=0;i<sizeof(sample_long);++i)sample_long[i]=(uint8_t)i;
    mode=2;writes=long_pos=0;bh_sound_new_round();bh_sound_post(SND_100,false);
    if(!setjmp(done))task_entry(NULL);
    assert(writes==4 && long_pos==1003 && bh_sound_max_gap_us()==20000);
    cleanup();puts("Actual audio worker: nonblocking bounded enqueue, urgent preemption, generation cancellation, allocation rollback: PASS");
}
