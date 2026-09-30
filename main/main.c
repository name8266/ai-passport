#include "battery_store.h"
#include "battery_ui.h"
#include "battery_web.h"
#include "battery_sound.h"
#include "bsp_battery.h"
#include "bsp_button.h"
#include "bsp_display.h"
#include "bsp_i2c.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include <stdio.h>
#include <string.h>

typedef struct { bsp_btn_t button; bsp_btn_ev_t event; } input_t;
static QueueHandle_t s_input;
static bat_db_t s_snapshot;
static battery_ui_state_t s_ui;
static uint32_t s_confirm_id,s_confirm_revision;
static bat_action_t s_confirm_action;
static uint32_t s_page_after;

static void on_key(bsp_btn_t button,bsp_btn_ev_t event,void *user) {
    (void)user;
    if (event!=BSP_BTN_CLICK && event!=BSP_BTN_LONG) return;
    const input_t input={button,event};
    if (s_input) (void)xQueueSend(s_input,&input,0);
}
static void web_toggle(void) {
    if (battery_web_running()) { battery_web_stop(); strcpy(s_ui.message,"Wi-Fi stopped"); }
    else {
        esp_err_t err=battery_web_start();
        strcpy(s_ui.message,err==ESP_OK ? "Phone workspace ready" : "Wi-Fi failed. OK to retry");
    }
    s_ui.web_running=battery_web_running();
    snprintf(s_ui.ssid,sizeof(s_ui.ssid),"%s",battery_web_ssid());
    snprintf(s_ui.password,sizeof(s_ui.password),"%s",battery_web_password());
}
static void process(const input_t *in) {
    s_ui.message[0]=0;
    if (in->event==BSP_BTN_LONG) {
        if (in->button!=BSP_BTN_OK) return;
        s_ui.confirm=false;
        if (s_ui.view==BAT_VIEW_HOME) s_ui.view=BAT_VIEW_WEB;
        else if (s_ui.view==BAT_VIEW_ACTIONS) s_ui.view=BAT_VIEW_ASSET;
        else {s_ui.view=BAT_VIEW_HOME;s_page_after=0;s_ui.selected=0;}
        return;
    }
    if (s_ui.view==BAT_VIEW_WEB) {
        if (in->button==BSP_BTN_OK) web_toggle();
        else {s_ui.view=BAT_VIEW_HOME;s_page_after=0;s_ui.selected=0;}
        return;
    }
    if (s_ui.view==BAT_VIEW_HOME) {
        if(in->button==BSP_BTN_OK){s_ui.pet_boop=2;strcpy(s_ui.message,"Purr... a little care matters");}
        else {s_ui.view=in->button==BSP_BTN_UP ? BAT_VIEW_REMINDERS : BAT_VIEW_ASSET;s_page_after=0;s_ui.selected=0;}
        return;
    }
    if(s_ui.view==BAT_VIEW_REMINDERS && in->button==BSP_BTN_OK) {
        strcpy(s_ui.message,battery_store_snooze() ? "Quiet for 15 minutes" : "Sync phone time first");return;
    }
    if (!s_snapshot.count) { if (in->button==BSP_BTN_OK) s_ui.view=BAT_VIEW_WEB; return; }
    if (s_ui.view==BAT_VIEW_ASSET || s_ui.view==BAT_VIEW_REMINDERS) {
        if(in->button==BSP_BTN_UP) {
            if(!s_ui.selected && s_page_after){s_page_after=battery_store_previous(s_page_after,s_ui.view==BAT_VIEW_REMINDERS ? -3 : -1);s_ui.selected=BAT_PAGE_SIZE-1;}
            else s_ui.selected=(s_ui.selected+s_snapshot.count-1)%s_snapshot.count;
        } else if(in->button==BSP_BTN_DOWN) {
            if(s_ui.selected+1==s_snapshot.count && s_ui.care.next_cursor) {
                s_page_after=s_ui.care.next_cursor;s_ui.selected=0;
            } else s_ui.selected=(s_ui.selected+1)%s_snapshot.count;
        }
        else {
            bat_action_t actions[3];
            if (battery_quick_actions(s_snapshot.assets[s_ui.selected].status,actions)) {
                s_ui.view=BAT_VIEW_ACTIONS; s_ui.action_index=0; s_ui.confirm=false;
            } else strcpy(s_ui.message,"Retired / manage on phone");
        }
        return;
    }
    bat_action_t actions[3];
    unsigned count=battery_quick_actions(s_snapshot.assets[s_ui.selected].status,actions);
    if (!count) { s_ui.view=BAT_VIEW_ASSET; return; }
    if (s_ui.action_index>=count) s_ui.action_index=0;
    if (in->button!=BSP_BTN_OK) {
        s_ui.confirm=false;
        s_ui.action_index=(s_ui.action_index+(in->button==BSP_BTN_UP ? count-1 : 1))%count;
    } else if (!s_ui.writable) strcpy(s_ui.message,"Storage unavailable");
    else if (!s_ui.confirm) {
        s_ui.confirm=true; s_confirm_id=s_snapshot.assets[s_ui.selected].id;
        s_confirm_revision=s_snapshot.revision; s_confirm_action=actions[s_ui.action_index];
    } else {
        bat_result_t result;
        esp_err_t err=battery_store_action(s_confirm_id,s_confirm_action,s_confirm_revision,&result);
        strcpy(s_ui.message,err!=ESP_OK ? "Save failed. Please retry" : result==BAT_OK ? "Saved to your collection" : "Changed / please try again");
        s_ui.confirm=false; s_ui.view=BAT_VIEW_ASSET;
    }
}
void app_main(void) {
    ESP_LOGI("battery_desk","Battery Desk / schema %u",BAT_SCHEMA);
    esp_err_t storage=battery_store_init();
    if (storage!=ESP_OK) ESP_LOGE("battery_desk","Storage locked: %s; no erase",esp_err_to_name(storage));
    if (bsp_display_init()!=ESP_OK || !bsp_lvgl_init()) {
        ESP_LOGE("battery_desk","Display initialization failed"); return;
    }
    bsp_display_backlight(75);
    bat_init(&s_snapshot);
    s_ui.device_soc=-1; s_ui.writable=battery_store_writable();
    battery_sound_init();
    bool battery=bsp_i2c_init()==ESP_OK && bsp_battery_init()==ESP_OK;
    s_input=xQueueCreate(12,sizeof(input_t));
    esp_err_t input=s_input ? bsp_button_init(on_key,NULL) : ESP_ERR_NO_MEM;
    if (input!=ESP_OK) strcpy(s_ui.message,"Buttons unavailable");
    if (bsp_lvgl_lock(1000)) { battery_ui_init(); bsp_lvgl_unlock(); }
    else { ESP_LOGE("battery_desk","UI lock failed"); return; }
    int64_t last_input=esp_timer_get_time()/1000,last_sample=-10000;
    unsigned brightness=75;
    int64_t last_alert=0,last_scan=0;uint32_t last_xp=0;
    /* app_main is the sole lifecycle/input worker. No page teardown, audio,
     * secondary ADC/I2C owner, or blocking I/O in LVGL/button callbacks. */
    for (;;) {
        input_t in;
        bool received=s_input && xQueueReceive(s_input,&in,pdMS_TO_TICKS(100))==pdTRUE;
        if (!s_input) vTaskDelay(pdMS_TO_TICKS(100));
        int64_t now=esp_timer_get_time()/1000;
        static int64_t last_refresh=-1000;
        if(!received && now-last_refresh<1000)continue;
        last_refresh=now;
        uint32_t old_revision=s_snapshot.revision;
        uint32_t selected_id=s_ui.selected<s_snapshot.count ? s_snapshot.assets[s_ui.selected].id : 0;
        s_ui.writable=battery_store_writable();
        if (!battery_store_page(&s_snapshot,&s_ui.care,s_page_after,"",s_ui.view==BAT_VIEW_REMINDERS ? -3 : -1)) {
            bat_init(&s_snapshot);memset(&s_ui.care,0,sizeof(s_ui.care));
            strcpy(s_ui.message,"Storage locked / no erase");
        }
        int selected=bat_find(&s_snapshot,selected_id);
        if (selected>=0) s_ui.selected=(unsigned)selected;
        else if (s_ui.selected>=s_snapshot.count) s_ui.selected=0;
        if (s_snapshot.revision!=old_revision && s_ui.confirm) {
            s_ui.confirm=false; s_ui.view=BAT_VIEW_ASSET; strcpy(s_ui.message,"Updated from phone");
        }
        if (received) {
            bool waking=brightness==0;
            last_input=now;
            if (!waking) process(&in);
            if (!battery_store_page(&s_snapshot,&s_ui.care,s_page_after,"",s_ui.view==BAT_VIEW_REMINDERS ? -3 : -1)) s_ui.writable=false;
        }
        if(!s_snapshot.count && s_page_after){s_page_after=0;}
        memset(&s_ui.selected_reminder,0,sizeof(s_ui.selected_reminder));
        if(s_ui.view==BAT_VIEW_REMINDERS && s_ui.selected<s_snapshot.count) {
            care_asset_t a;if(battery_store_get(s_snapshot.assets[s_ui.selected].id,&a)) {
                s_ui.selected_reminder.id=a.asset.id;s_ui.selected_reminder.reason=care_due(&a,battery_time_now(NULL),&s_ui.selected_reminder.due_at);
            }
        }
        if(s_ui.pet_boop)--s_ui.pet_boop;
        s_ui.epoch=battery_time_now(&s_ui.timezone);s_ui.speaker_available=battery_sound_available();
        if(s_ui.care.pet.xp>last_xp && last_scan) {
            s_ui.pet_boop=3;snprintf(s_ui.message,sizeof(s_ui.message),"Blue grew! +%lu care points",(unsigned long)(s_ui.care.pet.xp-last_xp));
            if(!care_quiet(&s_ui.care.pet,s_ui.epoch,s_ui.timezone))battery_sound_notify(true,s_ui.care.pet.volume);
        }
        last_xp=s_ui.care.pet.xp;last_scan=now;
        if(s_ui.care.due_count && !care_quiet(&s_ui.care.pet,s_ui.epoch,s_ui.timezone) && (!last_alert || s_ui.epoch-last_alert>=1800)) {
            battery_sound_notify(false,s_ui.care.pet.volume);last_alert=s_ui.epoch;
            snprintf(s_ui.message,sizeof(s_ui.message),"%lu reminders / UP to view",(unsigned long)s_ui.care.due_count);
        }
        unsigned target=now-last_input>120000 ? 0 : now-last_input>45000 ? 15 : 75;
        if (target!=brightness) { bsp_display_backlight(target); brightness=target; }
        s_ui.epoch=battery_time_now(&s_ui.timezone);
        if (now-last_sample>=10000) {
            s_ui.device_soc=battery ? bsp_battery_soc() : -1; last_sample=now;
            ESP_LOGD("battery_desk","heap=%u largest=%u stack=%u",(unsigned)esp_get_free_heap_size(),
                (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT),(unsigned)uxTaskGetStackHighWaterMark(NULL));
        }
        if (bsp_lvgl_lock(50)) { battery_ui_render(&s_snapshot,&s_ui); bsp_lvgl_unlock(); }
    }
}
