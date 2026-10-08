/* Passport Nav: dedicated device UI, independent of the original test menu. */
#include "bsp_battery.h"
#include "bsp_button.h"
#include "bsp_display.h"
#include "bsp_i2c.h"
#include "nav_packet.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "host/ble_gap.h"
#include "host/ble_gatt.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "lvgl.h"
#include "nimble/nimble_port.h"
#include "nvs_flash.h"
#include "os/os_mbuf.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include <string.h>

static const char *TAG="passport_nav";
static const char *DEVICE_NAME="Passport Nav";
/* Service 8A2FA760-11F0-4FD9-9B94-6E092E60E21A
 * Writer  8A2FA760-11F0-4FD9-9B94-6E092E60E21B */
static const ble_uuid128_t SERVICE_UUID=BLE_UUID128_INIT(
    0x1A,0xE2,0x60,0x2E,0x09,0x6E,0x94,0x9B,0xD9,0x4F,0xF0,0x11,0x60,0xA7,0x2F,0x8A);
static const ble_uuid128_t WRITE_UUID=BLE_UUID128_INIT(
    0x1B,0xE2,0x60,0x2E,0x09,0x6E,0x94,0x9B,0xD9,0x4F,0xF0,0x11,0x60,0xA7,0x2F,0x8A);

static QueueHandle_t packet_queue, key_queue;
static volatile bool connected;
static uint8_t address_type;
static nav_packet_t current;
static bool have_packet;
static int64_t last_received;
static int display_mode;
static bool full_brightness=true;
static lv_obj_t *status_label, *action_label, *distance_label, *speed_label;
static lv_obj_t *detail_label, *battery_label;
typedef struct { bsp_btn_t key; bsp_btn_ev_t event; } button_event_t;

static lv_obj_t *text_at(lv_obj_t *root,const char *str,int x,int y,
                         const lv_font_t *font,uint32_t color) {
    lv_obj_t *t=lv_label_create(root);
    lv_label_set_text(t,str);
    lv_obj_set_pos(t,x,y);
    lv_obj_set_style_text_font(t,font,0);
    lv_obj_set_style_text_color(t,lv_color_hex(color),0);
    return t;
}
static void rect(lv_obj_t *root,int x,int y,int w,int h) {
    lv_obj_t *p=lv_obj_create(root);
    lv_obj_set_pos(p,x,y);
    lv_obj_set_size(p,w,h);
    lv_obj_set_style_bg_color(p,lv_color_hex(0x18283D),0);
    lv_obj_set_style_border_width(p,0,0);
    lv_obj_set_style_radius(p,12,0);
    lv_obj_set_style_pad_all(p,0,0);
    lv_obj_remove_flag(p,LV_OBJ_FLAG_SCROLLABLE);
}
static void create_screen(void) {
    lv_obj_t *root=lv_obj_create(NULL);
    lv_obj_remove_flag(root,LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(root,lv_color_hex(0x0B1421),0);
    lv_obj_set_style_border_width(root,0,0);
    lv_obj_set_style_pad_all(root,0,0);
    text_at(root,"PASSPORT NAV",12,7,&lv_font_montserrat_14,0xF5F8FF);
    battery_label=text_at(root,"--%",194,7,&lv_font_montserrat_14,0x7DE3BC);
    status_label=text_at(root,"BLE: WAIT",12,30,&lv_font_montserrat_14,0xFFBC74);
    rect(root,10,55,220,119);
    action_label=text_at(root,"READY",22,73,&lv_font_montserrat_20,0x7DE3BC);
    distance_label=text_at(root,"-- m",22,110,&lv_font_montserrat_36,0xFFFFFF);
    rect(root,10,184,220,99);
    speed_label=text_at(root,"--",22,194,&lv_font_montserrat_36,0xFFFFFF);
    text_at(root,"km/h",147,210,&lv_font_montserrat_14,0xA6B9CE);
    detail_label=text_at(root,"Waiting for phone",22,256,&lv_font_montserrat_14,0x7DE3BC);
    text_at(root,"UP/DOWN:MODE  OK:LIGHT",10,297,&lv_font_montserrat_14,0xA6B9CE);
    lv_screen_load(root);
}
static const char *maneuver_label(uint8_t step) {
    switch(step) {
    case NAV_STRAIGHT: return "GO STRAIGHT";
    case NAV_LEFT: return "< TURN LEFT";
    case NAV_RIGHT: return "TURN RIGHT >";
    case NAV_UTURN: return "U-TURN";
    case NAV_ARRIVE: return "ARRIVED";
    case NAV_BEAR_LEFT: return "< KEEP LEFT";
    case NAV_BEAR_RIGHT: return "KEEP RIGHT >";
    default: return "NO TURN";
    }
}
static void update_display(lv_timer_t *timer) {
    (void)timer;
    button_event_t button;
    while (xQueueReceive(key_queue,&button,0)==pdTRUE) {
        if (button.event!=BSP_BTN_CLICK) continue;
        if (button.key==BSP_BTN_UP) display_mode=(display_mode+2)%3;
        else if (button.key==BSP_BTN_DOWN) display_mode=(display_mode+1)%3;
        else if (button.key==BSP_BTN_OK) {
            full_brightness=!full_brightness;
            bsp_display_backlight(full_brightness?100:30);
        }
    }
    nav_packet_t value;
    while (xQueueReceive(packet_queue,&value,0)==pdTRUE) {
        if (!have_packet || (int16_t)(value.sequence-current.sequence)>0) {
            current=value;
            have_packet=true;
            last_received=esp_timer_get_time()/1000;
        }
    }
    int percent=bsp_battery_soc();
    if(percent>=0) lv_label_set_text_fmt(battery_label,"%d%%",percent);
    else lv_label_set_text(battery_label,"--%");
    bool fresh=connected && have_packet && (esp_timer_get_time()/1000-last_received<3000);
    bool active=fresh && (current.flags & NAV_FLAG_ACTIVE);
    bool gps=fresh && (current.flags & NAV_FLAG_GPS);
    lv_label_set_text(status_label,!connected?"BLE: WAITING":
        !fresh?"BLE: STALE":!gps?"GPS: SEARCH":"GPS: LIVE");
    lv_obj_set_style_text_color(status_label,lv_color_hex(fresh?0x7DE3BC:0xFFBC74),0);
    if(!fresh) {
        lv_label_set_text(action_label,"NO NAV DATA");
        lv_label_set_text(distance_label,"--");
        lv_label_set_text(speed_label,"--");
        lv_label_set_text(detail_label,"Connect the iPhone app");
    } else if(display_mode==0) {
        lv_label_set_text(action_label,active?maneuver_label(current.maneuver):"NO ROUTE");
        if(active) lv_label_set_text_fmt(distance_label,"%u m",current.turn_meters);
        else lv_label_set_text(distance_label,"--");
        lv_label_set_text_fmt(speed_label,"%u",current.speed_tenths_kmh/10);
        lv_label_set_text_fmt(detail_label,"Trip %u.%02u km  %u min",
            current.remaining_tens_meters/100,current.remaining_tens_meters%100,
            current.remaining_seconds/60);
    } else if(display_mode==1) {
        lv_label_set_text(action_label,"GPS SPEED");
        lv_label_set_text_fmt(distance_label,"%u",current.speed_tenths_kmh/10);
        lv_label_set_text_fmt(speed_label,"%u",current.bearing_deg);
        lv_label_set_text(detail_label,"km/h above / heading below");
    } else {
        lv_label_set_text(action_label,"TRIP SUMMARY");
        lv_label_set_text_fmt(distance_label,"%u.%02u km",
            current.remaining_tens_meters/100,current.remaining_tens_meters%100);
        lv_label_set_text_fmt(speed_label,"%02u:%02u",current.eta_hour,current.eta_minute);
        lv_label_set_text_fmt(detail_label,"%u minutes left / ETA",
            current.remaining_seconds/60);
    }
}
static void on_button(bsp_btn_t key,bsp_btn_ev_t event,void *arg) {
    (void)arg;
    button_event_t b={.key=key,.event=event};
    if(key_queue) (void)xQueueSend(key_queue,&b,0);
}
static int on_write(uint16_t c,uint16_t a,struct ble_gatt_access_ctxt *ctx,void *arg) {
    (void)c;(void)a;(void)arg;
    if(ctx->op!=BLE_GATT_ACCESS_OP_WRITE_CHR) return BLE_ATT_ERR_REQ_NOT_SUPPORTED;
    if(OS_MBUF_PKTLEN(ctx->om)!=NAV_PACKET_SIZE) return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    uint8_t raw[NAV_PACKET_SIZE];
    nav_packet_t packet;
    if(os_mbuf_copydata(ctx->om,0,NAV_PACKET_SIZE,raw)!=0 ||
       !nav_packet_decode(raw,sizeof(raw),&packet)) return BLE_ATT_ERR_UNLIKELY;
    (void)xQueueOverwrite(packet_queue,&packet);
    return 0;
}
static const struct ble_gatt_chr_def characteristics[]={
    {.uuid=&WRITE_UUID.u,.access_cb=on_write,.flags=BLE_GATT_CHR_F_WRITE},
    {0}
};
static const struct ble_gatt_svc_def services[]={
    {.type=BLE_GATT_SVC_TYPE_PRIMARY,.uuid=&SERVICE_UUID.u,
     .characteristics=characteristics},
    {0}
};
static int gap_event(struct ble_gap_event *event,void *arg);
static int start_advertising(void) {
    struct ble_hs_adv_fields fields={0};
    fields.flags=BLE_HS_ADV_F_DISC_GEN|BLE_HS_ADV_F_BREDR_UNSUP;
    fields.uuids128=(ble_uuid128_t *)&SERVICE_UUID;
    fields.num_uuids128=1;
    fields.uuids128_is_complete=1;
    int rc=ble_gap_adv_set_fields(&fields);
    if(rc) return rc;
    struct ble_hs_adv_fields scan={0};
    scan.name=(const uint8_t *)"Passport Nav";
    scan.name_len=strlen("Passport Nav");
    scan.name_is_complete=1;
    rc=ble_gap_adv_rsp_set_fields(&scan);
    if(rc) return rc;
    struct ble_gap_adv_params params={0};
    params.conn_mode=BLE_GAP_CONN_MODE_UND;
    params.disc_mode=BLE_GAP_DISC_MODE_GEN;
    return ble_gap_adv_start(address_type,NULL,BLE_HS_FOREVER,&params,gap_event,NULL);
}
static int gap_event(struct ble_gap_event *event,void *arg) {
    (void)arg;
    switch(event->type) {
    case BLE_GAP_EVENT_CONNECT:
        connected=(event->connect.status==0);
        if(!connected) (void)start_advertising();
        break;
    case BLE_GAP_EVENT_DISCONNECT:
        connected=false;
        have_packet=false;
        (void)start_advertising();
        break;
    case BLE_GAP_EVENT_ADV_COMPLETE:
        if(!connected) (void)start_advertising();
        break;
    default: break;
    }
    return 0;
}
static void reset_ble(int reason) {
    connected=false;
    ESP_LOGW(TAG,"NimBLE reset: %d",reason);
}
static void sync_ble(void) {
    int rc=ble_hs_util_ensure_addr(0);
    if(!rc) rc=ble_hs_id_infer_auto(0,&address_type);
    if(!rc) rc=start_advertising();
    if(rc) ESP_LOGE(TAG,"BLE advertise: %d",rc);
}
static void ble_worker(void *arg) {
    (void)arg;
    nimble_port_run();
    (void)nimble_port_deinit();
    vTaskDelete(NULL);
}
void app_main(void) {
    ESP_ERROR_CHECK(bsp_i2c_init());
    ESP_ERROR_CHECK(bsp_display_init());
    if(!bsp_lvgl_init()) {ESP_LOGE(TAG,"LVGL init failed");return;}
    (void)bsp_battery_init();
    packet_queue=xQueueCreate(1,sizeof(nav_packet_t));
    key_queue=xQueueCreate(8,sizeof(button_event_t));
    if(!packet_queue||!key_queue) {ESP_LOGE(TAG,"Queue allocation failed");return;}
    (void)bsp_button_init(on_button,NULL);
    bsp_display_backlight(100);
    if(!bsp_lvgl_lock(1000)) {ESP_LOGE(TAG,"UI lock failed");return;}
    create_screen();
    (void)lv_timer_create(update_display,250,NULL);
    bsp_lvgl_unlock();
    esp_err_t nvs=nvs_flash_init();
    if(nvs!=ESP_OK) {ESP_LOGE(TAG,"NVS failed: %s",esp_err_to_name(nvs));return;}
    ESP_ERROR_CHECK(nimble_port_init());
    ble_svc_gap_init();
    ble_svc_gatt_init();
    if(ble_svc_gap_device_name_set(DEVICE_NAME)!=0 ||
       ble_gatts_count_cfg(services)!=0 ||
       ble_gatts_add_svcs(services)!=0) {
        ESP_LOGE(TAG,"GATT setup failed");return;
    }
    ble_hs_cfg.reset_cb=reset_ble;
    ble_hs_cfg.sync_cb=sync_ble;
    if(xTaskCreate(ble_worker,"nav_ble",4096,NULL,5,NULL)!=pdPASS)
        ESP_LOGE(TAG,"BLE worker start failed");
}
