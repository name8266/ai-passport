/*
 * Passport Notification Hub v0.1
 * ESP32-C3 / Bluedroid / Apple Notification Center Service consumer.
 * Receives only notification metadata and text from an explicitly paired iPhone.
 * No navigation, media, Internet, or phone-side privileged app.
 */
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "esp_log.h"
#include "esp_system.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_gap_ble_api.h"
#include "esp_gattc_api.h"
#include "esp_gatt_common_api.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "bsp_i2c.h"
#include "bsp_display.h"
#include "bsp_button.h"
#include "bsp_battery.h"
#include "lvgl.h"
#include "hub_protocol.h"

static const char *TAG="notify_hub";
#define HUB_LIMIT 8
#define INVALID 0
/* UUIDs in Bluetooth little-endian wire order. */
static const uint8_t ancs_service[16]=
  {0xD0,0x00,0x2D,0x12,0x1E,0x4B,0x0F,0xA4,0x99,0x4E,0xCE,0xB5,0x31,0xF4,0x05,0x79};
static const uint8_t ancs_source[16]=
  {0xBD,0x1D,0xA2,0x99,0xE6,0x25,0x58,0x8C,0xD9,0x42,0x01,0x63,0x0D,0x12,0xBF,0x9F};
static const uint8_t ancs_data[16]=
  {0xFB,0x7B,0x7C,0xCE,0x6A,0xB3,0x44,0xBE,0xB5,0x4B,0xD6,0x24,0xE9,0xC6,0xEA,0x22};
static const uint8_t ancs_control[16]=
  {0xD9,0xD9,0xAA,0xFD,0xBD,0x9B,0x21,0x98,0xA8,0x49,0xE1,0x45,0xF3,0xD8,0xD1,0x69};
/* Advertising *solicits* iPhone's ANCS (AD type 0x15), instead of
 * claiming to implement the ANCS GATT server. */
static uint8_t advertisement[]={
  2,0x01,0x06,
  17,0x15,0xD0,0x00,0x2D,0x12,0x1E,0x4B,0x0F,0xA4,
  0x99,0x4E,0xCE,0xB5,0x31,0xF4,0x05,0x79
};
static uint8_t scan_response[]={
  11,0x09,'N','o','t','i','f','y',' ','H','u','b'
};
static esp_ble_adv_params_t adv_params={
    .adv_int_min=0xF4, .adv_int_max=0xF4,
    .adv_type=ADV_TYPE_IND,
    .own_addr_type=BLE_ADDR_TYPE_PUBLIC,
    .channel_map=ADV_CHNL_ALL,
    .adv_filter_policy=ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY
};

typedef struct {
    uint32_t uid;
    uint8_t category;
    char app[HUB_APP_BYTES];
    char title[HUB_TITLE_BYTES];
    char body[HUB_BODY_BYTES];
} item_t;
typedef struct {
    uint8_t type; /* 0 insert/update, 1 remove, 2 clear session */
    item_t item;
} notice_evt_t;
typedef struct { bsp_btn_t btn; bsp_btn_ev_t ev; } button_t;
static QueueHandle_t notice_queue, button_queue;
static item_t recent[HUB_LIMIT];
static int recent_count, selected;
static lv_obj_t *top_status,*top_battery,*page_no,*app_name,*title_text,*body_text,*help_text;
static lv_font_t readable_font;
static uint32_t passkey;
static volatile bool paired, ready, connected;
static esp_gatt_if_t gatt_interface=ESP_GATT_IF_NONE;
static uint16_t conn_id, start_handle, end_handle, notification_handle, data_handle;
static uint16_t control_handle, notify_cccd, data_cccd;
static esp_bd_addr_t phone_address;
static hub_decoder_t decoder;
static bool requesting;
static uint32_t pending_uid;
static bool pending_valid;
static uint32_t in_flight_uid;
static int64_t request_started;
static int stage; /* 0 idle, 1 enabling data source, 2 enabling notification source, 3 ready */
static uint32_t received_count;

static bool uuid_is(const esp_bt_uuid_t *u,const uint8_t *v) {
    return u->len==ESP_UUID_LEN_128 && memcmp(u->uuid.uuid128,v,16)==0;
}
static void item_event(uint8_t type,const item_t *item) {
    notice_evt_t evt={.type=type};
    if(item) evt.item=*item;
    if(notice_queue) (void)xQueueSend(notice_queue,&evt,0);
}
static void reset_session(void) {
    ready=false; paired=false; connected=false;
    requesting=false; pending_valid=false; stage=0;
    start_handle=end_handle=notification_handle=data_handle=control_handle=0;
    notify_cccd=data_cccd=0;
    hub_decoder_begin(&decoder,0);
    item_event(2,NULL); /* purge private session data on disconnect */
}
static const char *category_name(int category) {
    static const char *const labels[]={
      "Other","Call","Missed call","Voicemail","Social","Calendar",
      "Email","News","Health","Business","Location","Entertainment"
    };
    return (category>=0 && category<12)?labels[category]:"Notification";
}
static void insert_item(const item_t *item) {
    for(int i=0;i<recent_count;i++) if(recent[i].uid==item->uid) {
        recent[i]=*item;
        if(selected==i) selected=0;
        return;
    }
    if(recent_count<HUB_LIMIT) recent_count++;
    for(int i=recent_count-1;i>0;i--) recent[i]=recent[i-1];
    recent[0]=*item;
    selected=0;
}
static void remove_item(uint32_t uid) {
    for(int i=0;i<recent_count;i++) if(recent[i].uid==uid) {
        for(int k=i;k<recent_count-1;k++) recent[k]=recent[k+1];
        recent_count--;
        if(selected>=recent_count) selected=recent_count>0?recent_count-1:0;
        return;
    }
}
static lv_obj_t *new_label(lv_obj_t *parent,const char *str,
                            int x,int y,int width,const lv_font_t *font,
                            uint32_t color) {
    lv_obj_t *label=lv_label_create(parent);
    lv_label_set_text(label,str);
    lv_obj_set_pos(label,x,y);
    lv_obj_set_width(label,width);
    lv_obj_set_style_text_font(label,font,0);
    lv_obj_set_style_text_color(label,lv_color_hex(color),0);
    lv_label_set_long_mode(label,LV_LABEL_LONG_WRAP);
    return label;
}
static void box(lv_obj_t *root,int x,int y,int w,int h) {
    lv_obj_t *p=lv_obj_create(root);
    lv_obj_set_pos(p,x,y);
    lv_obj_set_size(p,w,h);
    lv_obj_set_style_radius(p,12,0);
    lv_obj_set_style_bg_color(p,lv_color_hex(0x202F43),0);
    lv_obj_set_style_border_width(p,0,0);
    lv_obj_set_style_pad_all(p,0,0);
    lv_obj_remove_flag(p,LV_OBJ_FLAG_SCROLLABLE);
}
static void build_ui(void) {
    readable_font=lv_font_source_han_sans_sc_16_cjk;
    readable_font.fallback=&lv_font_montserrat_14;
    lv_obj_t *root=lv_obj_create(NULL);
    lv_obj_remove_flag(root,LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(root,lv_color_hex(0x091523),0);
    lv_obj_set_style_border_width(root,0,0);
    lv_obj_set_style_pad_all(root,0,0);
    new_label(root,"NOTIFY HUB",12,7,168,&lv_font_montserrat_20,0xF1F8FF);
    top_battery=new_label(root,"--%",198,8,38,&lv_font_montserrat_14,0x7EE6B3);
    top_status=new_label(root,"Waiting for iPhone",12,38,216,&lv_font_montserrat_14,0xFFBB70);
    box(root,9,73,222,217);
    page_no=new_label(root,"0 / 8",20,84,200,&lv_font_montserrat_14,0x7EE6B3);
    app_name=new_label(root,"No notifications",20,114,198,&readable_font,0xA6C4FF);
    title_text=new_label(root,"Pair from iPhone Bluetooth settings",20,148,198,
                         &readable_font,0xFFFFFF);
    body_text=new_label(root,"Your notifications stay on this device.",20,203,196,
                        &readable_font,0xCFDBE9);
    help_text=new_label(root,"UP/DOWN: BROWSE  OK: CLEAR",10,299,220,
                        &lv_font_montserrat_14,0x96A9BA);
    lv_screen_load(root);
}
static void render(void) {
    int battery=bsp_battery_soc();
    if(battery<0) lv_label_set_text(top_battery,"--%");
    else lv_label_set_text_fmt(top_battery,"%d%%",battery);
    if(!connected) lv_label_set_text_fmt(top_status,"Pair code: %06lu",(unsigned long)passkey);
    else if(!paired) lv_label_set_text_fmt(top_status,"Enter %06lu on iPhone",(unsigned long)passkey);
    else if(!ready) lv_label_set_text(top_status,"Paired - requesting access");
    else lv_label_set_text(top_status,"iPhone connected - ANCS");
    lv_obj_set_style_text_color(top_status,lv_color_hex(ready?0x7EE6B3:0xFFBB70),0);
    lv_label_set_text_fmt(page_no,"%d / %d   *%lu",
        recent_count?selected+1:0,recent_count,(unsigned long)received_count);
    if(recent_count==0) {
        lv_label_set_text(app_name,"INBOX EMPTY");
        lv_label_set_text(title_text,ready?"No notifications yet":"Waiting for permission");
        lv_label_set_text(body_text,ready?"New iPhone alerts appear here.":
            "Open Settings > Bluetooth, pair Notify Hub and allow notification sharing.");
    }else{
        item_t *it=&recent[selected];
        lv_label_set_text(app_name,it->app[0]?it->app:category_name(it->category));
        lv_label_set_text(title_text,it->title[0]?it->title:"Notification");
        lv_label_set_text(body_text,it->body[0]?it->body:"(No message preview)");
    }
}
static void refresh(lv_timer_t *timer) {
    (void)timer;
    button_t btn;
    while(xQueueReceive(button_queue,&btn,0)==pdTRUE) {
        if(btn.ev!=BSP_BTN_CLICK) continue;
        if(btn.btn==BSP_BTN_UP && recent_count)
            selected=(selected+recent_count-1)%recent_count;
        else if(btn.btn==BSP_BTN_DOWN && recent_count)
            selected=(selected+1)%recent_count;
        else if(btn.btn==BSP_BTN_OK) {
            recent_count=0;selected=0;received_count=0;
            memset(recent,0,sizeof(recent));
        }
    }
    notice_evt_t e;
    while(xQueueReceive(notice_queue,&e,0)==pdTRUE) {
        if(e.type==2) {
            recent_count=selected=0;received_count=0;
            memset(recent,0,sizeof(recent));
        } else if(e.type==1) remove_item(e.item.uid);
        else {insert_item(&e.item);received_count++;}
    }
    render();
}
static void on_button(bsp_btn_t btn,bsp_btn_ev_t ev,void *unused) {
    (void)unused;
    button_t item={.btn=btn,.ev=ev};
    if(button_queue) (void)xQueueSend(button_queue,&item,0);
}
static void request_next(void);
static void get_details(uint32_t uid) {
    if(!ready || !control_handle || !paired) return;
    if(requesting) {pending_uid=uid;pending_valid=true;return;}
    uint8_t cmd[11]={
        0, (uint8_t)uid,(uint8_t)(uid>>8),(uint8_t)(uid>>16),(uint8_t)(uid>>24),
        0, 1, HUB_TITLE_BYTES-1,0, 3,HUB_BODY_BYTES-1
    };
    /* Message attribute must be followed by its uint16 maximum length. */
    uint8_t request[12];
    memcpy(request,cmd,sizeof(cmd));
    request[11]=0;
    if(esp_ble_gattc_write_char(gatt_interface,conn_id,control_handle,
           sizeof(request),request,ESP_GATT_WRITE_TYPE_RSP,
           ESP_GATT_AUTH_REQ_MITM)==ESP_OK) {
        requesting=true;
        in_flight_uid=uid;
        request_started=esp_timer_get_time();
        hub_decoder_begin(&decoder,uid);
    }
}
static void request_next(void) {
    requesting=false;
    if(pending_valid && ready) {
        uint32_t uid=pending_uid;pending_valid=false;
        get_details(uid);
    }
}
static bool find_cccd(uint16_t characteristic,uint16_t *handle) {
    uint16_t count=0;
    if(esp_ble_gattc_get_attr_count(gatt_interface,conn_id,ESP_GATT_DB_DESCRIPTOR,
        start_handle,end_handle,characteristic,&count)!=ESP_GATT_OK || !count)
        return false;
    esp_gattc_descr_elem_t *list=calloc(count,sizeof(*list));
    if(!list) return false;
    bool found=false;
    if(esp_ble_gattc_get_all_descr(gatt_interface,conn_id,characteristic,
                                  list,&count,0)==ESP_GATT_OK) {
        for(int i=0;i<count;i++)
            if(list[i].uuid.len==ESP_UUID_LEN_16 &&
               list[i].uuid.uuid.uuid16==ESP_GATT_UUID_CHAR_CLIENT_CONFIG) {
                *handle=list[i].handle;found=true;break;
            }
    }
    free(list);
    return found;
}
static void gatt_event(esp_gattc_cb_event_t event,esp_gatt_if_t gi,
                       esp_ble_gattc_cb_param_t *p) {
    switch(event) {
    case ESP_GATTC_REG_EVT:
        if(p->reg.status==ESP_GATT_OK) {
            gatt_interface=gi;
            esp_ble_gap_set_device_name("Notify Hub");
            (void)esp_ble_gap_config_adv_data_raw(advertisement,sizeof(advertisement));
            (void)esp_ble_gap_config_scan_rsp_data_raw(scan_response,sizeof(scan_response));
        }
        break;
    case ESP_GATTC_CONNECT_EVT: {
        connected=true;paired=false;ready=false;
        memcpy(phone_address,p->connect.remote_bda,ESP_BD_ADDR_LEN);
        esp_ble_gatt_creat_conn_params_t args={0};
        memcpy(args.remote_bda,phone_address,ESP_BD_ADDR_LEN);
        args.remote_addr_type=p->connect.ble_addr_type;
        args.own_addr_type=BLE_ADDR_TYPE_PUBLIC;
        args.is_direct=true;
        (void)esp_ble_gattc_enh_open(gi,&args);
        break;
    }
    case ESP_GATTC_OPEN_EVT:
        if(p->open.status!=ESP_GATT_OK) break;
        conn_id=p->open.conn_id;
        (void)esp_ble_set_encryption(p->open.remote_bda,ESP_BLE_SEC_ENCRYPT_MITM);
        (void)esp_ble_gattc_send_mtu_req(gi,conn_id);
        break;
    case ESP_GATTC_CFG_MTU_EVT:
        if(p->cfg_mtu.status==ESP_GATT_OK) {
            esp_bt_uuid_t target={.len=ESP_UUID_LEN_128};
            memcpy(target.uuid.uuid128,ancs_service,16);
            (void)esp_ble_gattc_search_service(gi,conn_id,&target);
        }
        break;
    case ESP_GATTC_SEARCH_RES_EVT:
        if(uuid_is(&p->search_res.srvc_id.uuid,ancs_service)) {
            start_handle=p->search_res.start_handle;
            end_handle=p->search_res.end_handle;
        }
        break;
    case ESP_GATTC_SEARCH_CMPL_EVT:
        if(!start_handle || !end_handle || !paired) break;
        {
            uint16_t count=0;
            if(esp_ble_gattc_get_attr_count(gi,conn_id,
                ESP_GATT_DB_CHARACTERISTIC,start_handle,end_handle,INVALID,
                &count)!=ESP_GATT_OK || !count) break;
            esp_gattc_char_elem_t *list=calloc(count,sizeof(*list));
            if(!list) break;
            if(esp_ble_gattc_get_all_char(gi,conn_id,start_handle,end_handle,
                                         list,&count,0)==ESP_GATT_OK) {
                for(int i=0;i<count;i++) {
                    if(uuid_is(&list[i].uuid,ancs_source) &&
                       (list[i].properties & ESP_GATT_CHAR_PROP_BIT_NOTIFY))
                        notification_handle=list[i].char_handle;
                    else if(uuid_is(&list[i].uuid,ancs_data) &&
                            (list[i].properties & ESP_GATT_CHAR_PROP_BIT_NOTIFY))
                        data_handle=list[i].char_handle;
                    else if(uuid_is(&list[i].uuid,ancs_control) &&
                            (list[i].properties & ESP_GATT_CHAR_PROP_BIT_WRITE))
                        control_handle=list[i].char_handle;
                }
            }
            free(list);
            if(notification_handle && data_handle && control_handle) {
                stage=1;
                (void)esp_ble_gattc_register_for_notify(gi,phone_address,data_handle);
            }
        }
        break;
    case ESP_GATTC_REG_FOR_NOTIFY_EVT:
        if(p->reg_for_notify.status!=ESP_GATT_OK) break;
        if(stage==1 && p->reg_for_notify.handle==data_handle &&
           find_cccd(data_handle,&data_cccd)) {
            uint8_t enable[]={1,0};
            (void)esp_ble_gattc_write_char_descr(gi,conn_id,data_cccd,2,
                   enable,ESP_GATT_WRITE_TYPE_RSP,ESP_GATT_AUTH_REQ_MITM);
        } else if(stage==2 && p->reg_for_notify.handle==notification_handle &&
                  find_cccd(notification_handle,&notify_cccd)) {
            uint8_t enable[]={1,0};
            (void)esp_ble_gattc_write_char_descr(gi,conn_id,notify_cccd,2,
                   enable,ESP_GATT_WRITE_TYPE_RSP,ESP_GATT_AUTH_REQ_MITM);
        }
        break;
    case ESP_GATTC_WRITE_DESCR_EVT:
        if(p->write.status!=ESP_GATT_OK) break;
        if(stage==1 && p->write.handle==data_cccd) {
            stage=2;
            (void)esp_ble_gattc_register_for_notify(gi,phone_address,notification_handle);
        } else if(stage==2 && p->write.handle==notify_cccd) {
            ready=true;stage=3;
        }
        break;
    case ESP_GATTC_NOTIFY_EVT:
        if(!ready || !paired) break;
        if(p->notify.handle==notification_handle) {
            hub_event_t evt;
            if(!hub_event_parse(p->notify.value,p->notify.value_len,&evt)) break;
            item_t item={.uid=evt.uid,.category=evt.category};
            if(evt.event==2) item_event(1,&item);
            else {
                snprintf(item.app,sizeof(item.app),"%s",category_name(evt.category));
                strcpy(item.title,"New notification");
                item_event(0,&item);
                get_details(evt.uid);
            }
        } else if(p->notify.handle==data_handle && requesting) {
            hub_notice_t notice;
            if(hub_decoder_feed(&decoder,p->notify.value,p->notify.value_len,&notice)) {
                item_t item={.uid=notice.uid,.category=0};
                memcpy(item.app,notice.app,sizeof(item.app));
                memcpy(item.title,notice.title,sizeof(item.title));
                memcpy(item.body,notice.body,sizeof(item.body));
                item_event(0,&item);
                request_next();
            }
        }
        break;
    case ESP_GATTC_WRITE_CHAR_EVT:
        if(p->write.status!=ESP_GATT_OK) request_next();
        break;
    case ESP_GATTC_SRVC_CHG_EVT: {
        ready=false;stage=0;
        esp_bt_uuid_t target={.len=ESP_UUID_LEN_128};
        memcpy(target.uuid.uuid128,ancs_service,16);
        (void)esp_ble_gattc_search_service(gi,conn_id,&target);
        break;
    }
    case ESP_GATTC_DISCONNECT_EVT:
        reset_session();
        (void)esp_ble_gap_start_advertising(&adv_params);
        break;
    default:break;
    }
}
static void gap_event(esp_gap_ble_cb_event_t event,esp_ble_gap_cb_param_t *p) {
    static bool adv_ok,scan_ok;
    switch(event) {
    case ESP_GAP_BLE_ADV_DATA_RAW_SET_COMPLETE_EVT: adv_ok=true;break;
    case ESP_GAP_BLE_SCAN_RSP_DATA_RAW_SET_COMPLETE_EVT: scan_ok=true;break;
    case ESP_GAP_BLE_SEC_REQ_EVT:
        (void)esp_ble_gap_security_rsp(p->ble_security.ble_req.bd_addr,true);
        break;
    case ESP_GAP_BLE_AUTH_CMPL_EVT:
        paired=p->ble_security.auth_cmpl.success;
        if(!paired) ready=false;
        else if(start_handle) {
            esp_bt_uuid_t target={.len=ESP_UUID_LEN_128};
            memcpy(target.uuid.uuid128,ancs_service,16);
            (void)esp_ble_gattc_search_service(gatt_interface,conn_id,&target);
        }
        break;
    case ESP_GAP_BLE_PASSKEY_NOTIF_EVT:
        passkey=p->ble_security.key_notif.passkey;
        break;
    case ESP_GAP_BLE_NC_REQ_EVT:
        /* Deliberately reject unconfirmed numeric comparison. */
        (void)esp_ble_confirm_reply(p->ble_security.ble_req.bd_addr,false);
        break;
    default:break;
    }
    if(adv_ok && scan_ok) {
        adv_ok=false;scan_ok=false;
        (void)esp_ble_gap_start_advertising(&adv_params);
    }
}
void app_main(void) {
    ESP_ERROR_CHECK(bsp_i2c_init());
    ESP_ERROR_CHECK(bsp_display_init());
    if(!bsp_lvgl_init()) {ESP_LOGE(TAG,"LVGL unavailable");return;}
    (void)bsp_battery_init();
    button_queue=xQueueCreate(8,sizeof(button_t));
    notice_queue=xQueueCreate(12,sizeof(notice_evt_t));
    if(!button_queue || !notice_queue) {ESP_LOGE(TAG,"Queues unavailable");return;}
    (void)bsp_button_init(on_button,NULL);
    bsp_display_backlight(80);
    if(!bsp_lvgl_lock(1000)) {ESP_LOGE(TAG,"UI lock unavailable");return;}
    build_ui();
    (void)lv_timer_create(refresh,350,NULL);
    bsp_lvgl_unlock();

    esp_err_t err=nvs_flash_init();
    if(err!=ESP_OK) {ESP_LOGE(TAG,"NVS unavailable: %s",esp_err_to_name(err));return;}
    ESP_ERROR_CHECK(esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT));
    esp_bt_controller_config_t config=BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_bt_controller_init(&config));
    ESP_ERROR_CHECK(esp_bt_controller_enable(ESP_BT_MODE_BLE));
    ESP_ERROR_CHECK(esp_bluedroid_init());
    ESP_ERROR_CHECK(esp_bluedroid_enable());

    esp_ble_auth_req_t authentication=ESP_LE_AUTH_REQ_SC_MITM_BOND;
    esp_ble_io_cap_t io=ESP_IO_CAP_OUT;
    uint8_t key_size=16;
    uint8_t keys=ESP_BLE_ENC_KEY_MASK|ESP_BLE_ID_KEY_MASK;
    passkey=esp_random()%1000000;
    (void)esp_ble_gap_set_security_param(ESP_BLE_SM_SET_STATIC_PASSKEY,&passkey,sizeof(passkey));
    (void)esp_ble_gap_set_security_param(ESP_BLE_SM_AUTHEN_REQ_MODE,&authentication,sizeof(authentication));
    (void)esp_ble_gap_set_security_param(ESP_BLE_SM_IOCAP_MODE,&io,sizeof(io));
    (void)esp_ble_gap_set_security_param(ESP_BLE_SM_MAX_KEY_SIZE,&key_size,sizeof(key_size));
    (void)esp_ble_gap_set_security_param(ESP_BLE_SM_SET_INIT_KEY,&keys,sizeof(keys));
    (void)esp_ble_gap_set_security_param(ESP_BLE_SM_SET_RSP_KEY,&keys,sizeof(keys));
    (void)esp_ble_gatt_set_local_mtu(247);
    ESP_ERROR_CHECK(esp_ble_gap_register_callback(gap_event));
    ESP_ERROR_CHECK(esp_ble_gattc_register_callback(gatt_event));
    ESP_ERROR_CHECK(esp_ble_gattc_app_register(0));
}
