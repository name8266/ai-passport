#include "pusher_fx.h"
#include "pusher_sounds.h"
#include "pusher_store.h"
#include "bsp_audio.h"
#include "bsp_display.h"
#include "bsp_battery.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

static TaskHandle_t s_worker;
static volatile bool s_muted;
static volatile uint32_t s_settings_revision;
bool cp_fx_muted(void){return s_muted;}
void cp_fx_mute(bool muted){s_muted=muted;++s_settings_revision;if(s_worker)xTaskNotify(s_worker,0,eNoAction);}
static QueueHandle_t s_store;
static volatile uint32_t s_saved;
static volatile bool s_save_failed;
uint32_t cp_fx_saved_generation(void){return s_saved;}
bool cp_fx_save_failed(void){return s_save_failed;}
void cp_fx_store(const cp_records_t *b){if(s_store)xQueueOverwrite(s_store,b);else s_save_failed=true;}
static volatile int s_battery=-1;
int cp_fx_battery(void){return s_battery;}
static const char *TAG="coin_fx";
static void worker(void *arg){
    (void)arg;bool audio=bsp_audio_init()==ESP_OK;
    if(audio)audio=bsp_audio_set_format(16000,16,1)==ESP_OK;
    bool audio_muted=s_muted;
    if(audio)bsp_audio_set_volume(audio_muted?0:55);
    ESP_LOGI(TAG,"Audio %s; jackpot backlight pulses 65/100 percent; stack_free=%u",audio?"ready":"unavailable",(unsigned)uxTaskGetStackHighWaterMark(NULL));
    uint32_t pending=0,settings_saved=0;TickType_t battery_at=0;
    while(true){
        if(audio_muted!=s_muted){audio_muted=s_muted;if(audio)bsp_audio_set_volume(audio_muted?0:55);}
        uint32_t revision=s_settings_revision;
        if(revision!=settings_saved){bool muted=s_muted;if(cp_store_set_muted(muted)){settings_saved=revision;ESP_LOGI(TAG,"SETTINGS muted=%d saved=1",muted);}else ESP_LOGW(TAG,"Settings save failed; will retry");}
        cp_records_t board;
        if(s_store&&xQueueReceive(s_store,&board,0)==pdTRUE){s_save_failed=!cp_store_write(&board);if(!s_save_failed)s_saved=board.generation;}
        if(xTaskGetTickCount()-battery_at>=pdMS_TO_TICKS(2000)){s_battery=bsp_battery_soc();battery_at=xTaskGetTickCount();}
        uint32_t incoming=0;
        if(!pending)xTaskNotifyWait(0,UINT32_MAX,&incoming,pdMS_TO_TICKS(500));
        pending|=incoming;
        if(pending&CP_FX_RESET){bsp_display_backlight(85);pending=0;continue;}
        if(!pending)continue;
        uint32_t effect=pending&CP_FX_JACKPOT?CP_FX_JACKPOT:pending&CP_FX_HOLE?CP_FX_HOLE:pending&CP_FX_EVENT?CP_FX_EVENT:pending&CP_FX_SPIN?CP_FX_SPIN:pending&CP_FX_PRIZE?CP_FX_PRIZE:CP_FX_COIN;
        pending&=~effect;
        const int16_t *pcm=effect==CP_FX_JACKPOT?cp_sound_jackpot:effect==CP_FX_HOLE?cp_sound_hole:effect==CP_FX_EVENT?cp_sound_event:effect==CP_FX_SPIN?cp_sound_spin:effect==CP_FX_PRIZE?cp_sound_prize:cp_sound_coin;
        size_t count=effect==CP_FX_JACKPOT?sizeof(cp_sound_jackpot)/2:effect==CP_FX_HOLE?sizeof(cp_sound_hole)/2:effect==CP_FX_EVENT?sizeof(cp_sound_event)/2:effect==CP_FX_SPIN?sizeof(cp_sound_spin)/2:effect==CP_FX_PRIZE?sizeof(cp_sound_prize)/2:sizeof(cp_sound_coin)/2;
        unsigned pulses=0;
        for(size_t at=0;at<count;at+=256){
            if(audio_muted!=s_muted){audio_muted=s_muted;if(audio)bsp_audio_set_volume(audio_muted?0:55);}
            size_t n=count-at;if(n>256)n=256;
            if(effect==CP_FX_JACKPOT){unsigned phase=(unsigned)(at/2000);
                if(phase!=pulses||!at){bsp_display_backlight(phase&1?65:100);pulses=phase;}}
            if(audio&&!s_muted){if(bsp_audio_write(pcm+at,n*2)!=ESP_OK){audio=false;ESP_LOGW(TAG,"PCM write failed");}}
            else vTaskDelay(pdMS_TO_TICKS(16));
            incoming=0;xTaskNotifyWait(0,UINT32_MAX,&incoming,0);pending|=incoming;
            if((pending&CP_FX_RESET)||((pending&CP_FX_JACKPOT)&&effect!=CP_FX_JACKPOT))break;
        }
        if(effect==CP_FX_JACKPOT)bsp_display_backlight(85);
        ESP_LOGI(TAG,"Effect %lu complete: audio=%d muted=%d pulses=%u",(unsigned long)effect,audio,s_muted,effect==CP_FX_JACKPOT?pulses+1:0);
    }
}
void cp_fx_start(void){
    if(!s_store)s_store=xQueueCreate(1,sizeof(cp_records_t));
    if(!s_worker&&xTaskCreate(worker,"coin_fx",4096,NULL,2,&s_worker)!=pdPASS)ESP_LOGE(TAG,"Effect worker allocation failed");
}
void cp_fx_notify(uint32_t effects){if(s_worker)xTaskNotify(s_worker,effects,eSetBits);}
