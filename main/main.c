#include "battery_store.h"
#include "battery_ui.h"
#include "battery_web.h"
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
        else s_ui.view=BAT_VIEW_HOME;
        return;
    }
    if (s_ui.view==BAT_VIEW_WEB) {
        if (in->button==BSP_BTN_OK) web_toggle();
        else s_ui.view=BAT_VIEW_HOME;
        return;
    }
    if (s_ui.view==BAT_VIEW_HOME) {
        s_ui.view=in->button==BSP_BTN_OK ? BAT_VIEW_ASSET : BAT_VIEW_WEB;
        return;
    }
    if (!s_snapshot.count) { if (in->button==BSP_BTN_OK) s_ui.view=BAT_VIEW_WEB; return; }
    if (s_ui.view==BAT_VIEW_ASSET) {
        if (in->button==BSP_BTN_UP) s_ui.selected=(s_ui.selected+s_snapshot.count-1)%s_snapshot.count;
        else if (in->button==BSP_BTN_DOWN) s_ui.selected=(s_ui.selected+1)%s_snapshot.count;
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
    s_ui.device_soc=-1; s_ui.writable=battery_store_writable();
    bool battery=bsp_i2c_init()==ESP_OK && bsp_battery_init()==ESP_OK;
    s_input=xQueueCreate(12,sizeof(input_t));
    esp_err_t input=s_input ? bsp_button_init(on_key,NULL) : ESP_ERR_NO_MEM;
    if (input!=ESP_OK) strcpy(s_ui.message,"Buttons unavailable");
    if (bsp_lvgl_lock(1000)) { battery_ui_init(); bsp_lvgl_unlock(); }
    else { ESP_LOGE("battery_desk","UI lock failed"); return; }
    int64_t last_input=esp_timer_get_time()/1000,last_sample=-10000;
    unsigned brightness=75;
    /* app_main is the sole lifecycle/input worker. No page teardown, audio,
     * secondary ADC/I2C owner, or blocking I/O in LVGL/button callbacks. */
    for (;;) {
        input_t in;
        bool received=s_input && xQueueReceive(s_input,&in,pdMS_TO_TICKS(100))==pdTRUE;
        if (!s_input) vTaskDelay(pdMS_TO_TICKS(100));
        int64_t now=esp_timer_get_time()/1000;
        uint32_t old_revision=s_snapshot.revision;
        uint32_t selected_id=s_ui.selected<s_snapshot.count ? s_snapshot.assets[s_ui.selected].id : 0;
        if (!battery_store_snapshot(&s_snapshot)) continue;
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
            if (!battery_store_snapshot(&s_snapshot)) continue;
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
