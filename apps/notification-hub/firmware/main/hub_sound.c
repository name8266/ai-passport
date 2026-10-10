#include "hub_sound.h"
#include <stdatomic.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "bsp_audio.h"
#define SAVE 1u
#define ALERT 2u
#define PREVIEW 4u
static TaskHandle_t worker;
static atomic_uint desired;
static atomic_bool failed,save_failed;
static atomic_uint resource_owner; /* 0=free, 1=audio, 2=HTTPS */
static const char *TAG="hub_sound";
static unsigned pack(hub_sound_config_t c) {return (c.enabled?1u:0u)|((unsigned)c.tone<<8)|((unsigned)c.volume<<16);}
hub_sound_config_t hub_sound_get(void) {
    unsigned bits=atomic_load(&desired);
    return (hub_sound_config_t){(bits&1)!=0,(uint8_t)(bits>>8),(uint8_t)(bits>>16)};
}
bool hub_sound_error(void) {return atomic_load(&failed)||atomic_load(&save_failed);}
static bool save(hub_sound_config_t c) {
    nvs_handle_t h;if(nvs_open("hub_sound",NVS_READWRITE,&h)!=ESP_OK) return false;
    esp_err_t e=nvs_set_u32(h,"config",pack(c));if(e==ESP_OK)e=nvs_commit(h);nvs_close(h);return e==ESP_OK;
}
bool hub_sound_network_begin(void) {
    for(unsigned i=0;i<50;i++) {
        unsigned expected=0;
        if(atomic_compare_exchange_strong(&resource_owner,&expected,2)) return true;
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    return false;
}
void hub_sound_network_end(void) {atomic_store(&resource_owner,0);}
static bool play(hub_sound_config_t c) {
    if(!c.enabled) return true;
    unsigned expected=0;
    if(!atomic_compare_exchange_strong(&resource_owner,&expected,1)) return false;
    esp_err_t e=bsp_audio_init();
    if(e==ESP_OK) e=bsp_audio_set_format(HUB_SOUND_RATE,16,1);
    if(e==ESP_OK) bsp_audio_set_volume(c.volume);
    int16_t pcm[160];size_t total=hub_tone_samples(c.tone);
    for(size_t i=0;e==ESP_OK && i<total && hub_sound_get().enabled;i+=160) {
        size_t count=total-i<160?total-i:160;
        for(size_t k=0;k<count;k++) pcm[k]=hub_tone_sample(c.tone,i+k);
        e=bsp_audio_write(pcm,count*sizeof(*pcm));
    }
    /* Drain the short DMA tail before suspending; mute aborts within one chunk. */
    if(e==ESP_OK && hub_sound_get().enabled) vTaskDelay(pdMS_TO_TICKS(100));
    esp_err_t cleanup=bsp_audio_deinit();if(e==ESP_OK)e=cleanup;
    atomic_store(&failed,e!=ESP_OK);
    if(e!=ESP_OK)ESP_LOGW(TAG,"Chime unavailable: %s",esp_err_to_name(e));
    atomic_store(&resource_owner,0);
    return true;
}
static void task(void *arg) {
    (void)arg;uint64_t last=0;bool has_last=false;
    for(;;) {
        uint32_t bits=0;xTaskNotifyWait(0,UINT32_MAX,&bits,portMAX_DELAY);
        hub_sound_config_t c=hub_sound_get();
        if(bits&SAVE) atomic_store(&save_failed,!save(c));
        uint64_t now=(uint64_t)esp_timer_get_time()/1000;
        if((bits&PREVIEW) && c.enabled) {
            if(!play(c)) {vTaskDelay(pdMS_TO_TICKS(100));xTaskNotify(worker,PREVIEW,eSetBits);}
        }
        else if((bits&ALERT) && hub_alert_eligible(c,0,0,now,last,has_last)) {
            if(play(c)) {last=now;has_last=true;}
            else {vTaskDelay(pdMS_TO_TICKS(100));xTaskNotify(worker,ALERT,eSetBits);}
        }
    }
}
bool hub_sound_start(void) {
    hub_sound_config_t c=hub_sound_defaults();
    nvs_handle_t h;uint8_t enabled=0,tone=0,volume=0;
    bool loaded=false;
    if(nvs_open("hub_ai",NVS_READONLY,&h)==ESP_OK) {
        if(nvs_get_u8(h,"sound_on",&enabled)==ESP_OK &&
           nvs_get_u8(h,"sound_tone",&tone)==ESP_OK &&
           nvs_get_u8(h,"sound_volume",&volume)==ESP_OK) {
            hub_sound_config_t stored={enabled!=0,tone,volume};
            if(hub_sound_valid(stored)) {c=stored;loaded=true;}
        }
        nvs_close(h);
    }
    /* Preserve settings written by the previous on-device sound page. */
    if(!loaded) {
        uint32_t bits=0;
        if(nvs_open("hub_sound",NVS_READONLY,&h)==ESP_OK) {
            if(nvs_get_u32(h,"config",&bits)==ESP_OK) {
                hub_sound_config_t stored={(bits&1)!=0,(uint8_t)(bits>>8),
                                            (uint8_t)(bits>>16)};
                if(hub_sound_valid(stored)) c=stored;
            }
            nvs_close(h);
        }
    }
    atomic_store(&desired,pack(c));
    bool ok=xTaskCreate(task,"sound_worker",4096,NULL,3,&worker)==pdPASS;
    atomic_store(&failed,!ok);return ok;
}
void hub_sound_set(hub_sound_config_t c) {
    if(!hub_sound_valid(c)) return;
    atomic_store(&desired,pack(c));
    if(worker) xTaskNotify(worker,SAVE,eSetBits);
}
void hub_sound_notify(uint8_t event,uint8_t flags) {
    if(worker && hub_alert_eligible(hub_sound_get(),event,flags,0,0,false)) xTaskNotify(worker,ALERT,eSetBits);
}
void hub_sound_preview(void) {if(worker && hub_sound_get().enabled)xTaskNotify(worker,PREVIEW,eSetBits);}
