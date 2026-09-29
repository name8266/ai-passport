#include "tarot_audio.h"
#include <stdint.h>
#include "bsp_audio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#define AUDIO_RATE 16000
#define AUDIO_CHUNK 128
typedef enum{AUDIO_REVEAL=1,AUDIO_CONFIRM=2,AUDIO_SUSPEND=3,AUDIO_RESUME=4}audio_event_t;static const char*TAG="tarot_audio";static QueueHandle_t s_queue;static volatile bool s_enabled=true;static bool s_sleeping;
static void play_note(unsigned f,unsigned ms,int a){int16_t s[AUDIO_CHUNK];unsigned total=AUDIO_RATE*ms/1000,phase=0,period=AUDIO_RATE/f;while(total){unsigned n=total<AUDIO_CHUNK?total:AUDIO_CHUNK;for(unsigned i=0;i<n;++i){int v=phase<period/2?a:-a;if(total<AUDIO_RATE/100)v=v*(int)total/(AUDIO_RATE/100);s[i]=(int16_t)v;phase=(phase+1)%period;}if(bsp_audio_write(s,n*sizeof(s[0]))!=ESP_OK)return;total-=n;}}
static bool wake_if_needed(void){if(!s_sleeping)return true;if(bsp_audio_wake()!=ESP_OK){ESP_LOGE(TAG,"audio wake failed");return false;}s_sleeping=false;return true;}
static void audio_task(void*arg){(void)arg;audio_event_t e;for(;;){if(xQueueReceive(s_queue,&e,portMAX_DELAY)!=pdTRUE)continue;if(e==AUDIO_SUSPEND){if(!s_sleeping){if(bsp_audio_sleep()==ESP_OK)s_sleeping=true;else ESP_LOGE(TAG,"audio suspend failed");}continue;}if(e==AUDIO_RESUME){(void)wake_if_needed();continue;}if(!s_enabled||!wake_if_needed())continue;if(bsp_audio_set_format(AUDIO_RATE,16,1)!=ESP_OK)continue;bsp_audio_set_volume(42);if(e==AUDIO_REVEAL){play_note(440,55,2800);play_note(660,85,2400);}else if(e==AUDIO_CONFIRM)play_note(740,55,2200);}}
bool tarot_audio_init(void){if(s_queue)return true;if(bsp_audio_init()!=ESP_OK)return false;s_queue=xQueueCreate(6,sizeof(audio_event_t));return s_queue&&xTaskCreate(audio_task,"tarot_audio",3072,NULL,4,NULL)==pdPASS;}void tarot_audio_set_enabled(bool e){s_enabled=e;}static void enqueue(audio_event_t e){if(s_queue&&xQueueSend(s_queue,&e,0)!=pdTRUE)ESP_LOGW(TAG,"audio queue full; event=%d",e);}void tarot_audio_play_reveal(void){if(s_enabled)enqueue(AUDIO_REVEAL);}void tarot_audio_play_confirm(void){if(s_enabled)enqueue(AUDIO_CONFIRM);}void tarot_audio_suspend(void){enqueue(AUDIO_SUSPEND);}void tarot_audio_resume(void){enqueue(AUDIO_RESUME);}
