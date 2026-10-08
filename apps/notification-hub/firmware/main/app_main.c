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
#include "freertos/task.h"
#include "bsp_i2c.h"
#include "bsp_display.h"
#include "bsp_button.h"
#include "bsp_battery.h"
#include "lvgl.h"
#include "hub_protocol.h"
#include "hub_archive.h"
#include "hub_ai.h"

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
typedef struct { bsp_btn_t btn; bsp_btn_ev_t ev; } button_t;
typedef enum { ARCHIVE_SAVE=1, ARCHIVE_GROUP=2, ARCHIVE_RECORD=3, ARCHIVE_AI_LAST=4 } archive_job_kind_t;
typedef struct {
    archive_job_kind_t kind;
    hub_archive_record_t record;
    char app[HUB_APP_BYTES];
    uint32_t ordinal;
} archive_job_t;
typedef struct {
    archive_job_kind_t kind;
    bool found;
    uint32_t total_groups;
    hub_archive_group_t group;
    hub_archive_group_t previous;
    hub_archive_group_t next;
    hub_archive_record_t record;
    hub_ai_digest_t digest;
} archive_reply_t;
typedef enum { AI_LOAD_BATCH=1, AI_STORE_DIGEST=2 } ai_archive_kind_t;
typedef struct {
    ai_archive_kind_t kind;
    uint32_t after_sequence;
    uint8_t max_records;
    hub_ai_digest_t digest;
} ai_archive_req_t;
typedef struct {
    bool success;
    ai_archive_kind_t kind;
    hub_ai_batch_t batch;
} ai_archive_resp_t;

static QueueHandle_t button_queue, archive_jobs, archive_replies;
static QueueHandle_t ai_requests, ai_replies;
static volatile bool ai_configured, ai_online, ai_enabled, ai_busy;
static volatile bool ai_failed;
static volatile bool ai_run_now;
static hub_ai_digest_t latest_digest;
static bool latest_digest_ready;
static uint16_t digest_page;
static hub_archive_t archive_database; /* only archive_task accesses fields */
static volatile bool archive_loaded, archive_error, archive_full, archive_dropped;
static enum { VIEW_GROUPS=0, VIEW_LIST=1, VIEW_DETAIL=2, VIEW_DIGEST=3 } view_mode;
static int group_cursor, record_cursor;
static uint32_t visible_group_count, visible_record_count;
static char selected_app[HUB_APP_BYTES];
static hub_archive_group_t previous_group, next_group;
static hub_archive_record_t selected_record;
static bool selected_record_valid;
static int64_t next_summary_request;
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
#define ANCS_REQUEST_BACKLOG 32
static uint32_t pending_uids[ANCS_REQUEST_BACKLOG];
static uint8_t pending_head, pending_tail, pending_count;
static uint32_t in_flight_uid;
static int64_t request_started;
static int stage;
static uint32_t received_count;

static bool post_job(const archive_job_t *job) {
    if(!archive_jobs || xQueueSend(archive_jobs,job,0)!=pdTRUE) {
        if(job->kind==ARCHIVE_SAVE) archive_dropped=true; /* Don't falsely promise every alert was archived. */
        return false;
    }
    return true;
}
static void request_group(int index) {
    archive_job_t request={.kind=ARCHIVE_GROUP,.ordinal=(uint32_t)index};
    (void)post_job(&request);
}
static void request_record(void) {
    archive_job_t request={.kind=ARCHIVE_RECORD,.ordinal=(uint32_t)record_cursor};
    snprintf(request.app,sizeof(request.app),"%s",selected_app);
    (void)post_job(&request);
}
static void archive_capture(const item_t *source,hub_archive_kind_t kind) {
    archive_job_t job={.kind=ARCHIVE_SAVE};
    job.record.uid=source->uid;
    job.record.category=source->category;
    job.record.kind=kind;
    snprintf(job.record.app,sizeof(job.record.app),"%s",
             kind==HUB_ARCHIVE_SOURCE?"Unresolved":source->app);
    snprintf(job.record.title,sizeof(job.record.title),"%s",source->title);
    snprintf(job.record.body,sizeof(job.record.body),"%s",source->body);
    (void)post_job(&job);
}
/* A summary request asks for a page of unprocessed archival snapshots.
 * The archive task alone owns the FAT journal and digest file.
 */
static void handle_ai_archive_request(const ai_archive_req_t *request) {
    ai_archive_resp_t reply={.kind=request->kind};
    if(request->kind==AI_LOAD_BATCH) {
        reply.success=hub_archive_collect_since(&archive_database,
                    request->after_sequence,request->max_records,&reply.batch);
    }else if(request->kind==AI_STORE_DIGEST) {
        /* Commit summary bytes before advancing the NVS checkpoint. */
        reply.success=hub_archive_save_digest(&archive_database,&request->digest);
        if(reply.success)
            reply.success=hub_ai_advance_cursor(request->digest.processed_through);
        if(reply.success) {
            archive_job_t ui_request={.kind=ARCHIVE_AI_LAST};
            (void)post_job(&ui_request);
        }
    }
    if(ai_replies) (void)xQueueOverwrite(ai_replies,&reply);
}
static void ai_task(void *arg) {
    (void)arg;
    hub_ai_connection_t conn;
    if(!hub_ai_load_connection(&conn)) {
        ESP_LOGW(TAG,"AI disabled: Wi-Fi/gateway not provisioned");
        vTaskDelete(NULL);return;
    }
    ai_configured=true;
    if(!hub_ai_connect_wifi(&conn)) {
        ESP_LOGW(TAG,"AI disabled: Wi-Fi startup failed");
        ai_failed=true;vTaskDelete(NULL);return;
    }
    hub_ai_settings_t settings={0};
    int64_t next_config=0, next_run=0;
    while(true) {
        int64_t now=esp_timer_get_time()/1000000;
        if(now>=next_config) {
            hub_ai_settings_t changed;
            if(hub_ai_fetch_settings(&conn,&changed)) {
                ai_online=true;
                if(changed.enabled && !settings.enabled)
                    next_run=now+changed.interval_minutes*60;
                settings=changed;
                ai_enabled=settings.enabled;
            }else ai_online=false;
            next_config=now+60;
        }
        if(ai_online && settings.enabled && (ai_run_now || now>=next_run)) {
            ai_run_now=false;
            ai_busy=true;
            ai_failed=false;
            uint32_t cursor=hub_ai_read_cursor();
            int processed_batches=0;
            bool pending_more=false;
            for(int batch_index=0;batch_index<4;batch_index++) {
                ai_archive_req_t ask={.kind=AI_LOAD_BATCH,
                    .after_sequence=cursor,
                    .max_records=(uint8_t)(settings.max_records<HUB_AI_MAX_BATCH?
                                         settings.max_records:HUB_AI_MAX_BATCH)};
                if(xQueueSend(ai_requests,&ask,pdMS_TO_TICKS(1000))!=pdTRUE)
                    {ai_failed=true;break;}
                ai_archive_resp_t response;
                if(xQueueReceive(ai_replies,&response,pdMS_TO_TICKS(5000))!=pdTRUE ||
                   response.kind!=AI_LOAD_BATCH || !response.success) {
                    ai_failed=true;break;
                }
                if(response.batch.count==0) {pending_more=false;break;}
                hub_ai_digest_t result;
                if(!hub_ai_summarize(&conn,&response.batch,&result)) {
                    ai_failed=true;break;
                }
                ai_archive_req_t save={.kind=AI_STORE_DIGEST,.digest=result};
                if(xQueueSend(ai_requests,&save,pdMS_TO_TICKS(1000))!=pdTRUE ||
                   xQueueReceive(ai_replies,&response,pdMS_TO_TICKS(8000))!=pdTRUE ||
                   response.kind!=AI_STORE_DIGEST || !response.success) {
                    ai_failed=true;break;
                }
                cursor=result.processed_through;
                processed_batches++;
                pending_more=true;
                vTaskDelay(pdMS_TO_TICKS(2000));
            }
            now=esp_timer_get_time()/1000000;
            /* Throttle burst costs. Backlog stays on device and is handled
             * in later windows, with NVS cursor unchanged after failures. */
            next_run=now+(ai_failed?120:
                     pending_more && processed_batches==4?60:
                     settings.interval_minutes*60);
            ai_busy=false;
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
/* Exclusively owns FATFS access; filesystem work never runs inside BLE callback. */
static void archive_task(void *arg) {
    (void)arg;
    bool ok=hub_archive_open(&archive_database,esp_random());
    archive_error=!ok;
    archive_loaded=ok;
    archive_full=archive_database.full;
    if(ok) {
        archive_reply_t start={.kind=ARCHIVE_GROUP};
        uint32_t total=0;
        hub_archive_group_t g;
        while(hub_archive_get_group(&archive_database,total,&g)) total++;
        start.total_groups=total;
        start.found=hub_archive_get_group(&archive_database,0,&start.group);
        (void)xQueueOverwrite(archive_replies,&start);
    }
    archive_job_t load_latest={.kind=ARCHIVE_AI_LAST};
    (void)post_job(&load_latest);
    archive_job_t job;
    while(true) {
        ai_archive_req_t request;
        if(xQueueReceive(ai_requests,&request,0)==pdTRUE) {
            if(ok) handle_ai_archive_request(&request);
            else {
                ai_archive_resp_t r={.kind=request.kind,.success=false};
                (void)xQueueOverwrite(ai_replies,&r);
            }
        }
        if(xQueueReceive(archive_jobs,&job,pdMS_TO_TICKS(150))!=pdTRUE) continue;
        if(!ok) continue;
        if(job.kind==ARCHIVE_SAVE) {
            if(!hub_archive_capture(&archive_database,&job.record)) {
                archive_full=archive_database.full;
                archive_error=archive_database.failed;
                archive_dropped=true;
            }
        } else {
            archive_reply_t reply={.kind=job.kind};
            if(job.kind==ARCHIVE_GROUP) {
                hub_archive_group_t g;
                uint32_t total=0;
                while(hub_archive_get_group(&archive_database,total,&g)) total++;
                reply.total_groups=total;
                reply.found=hub_archive_get_group(&archive_database,job.ordinal,
                                                   &reply.group);
                if(total>0) {
                    (void)hub_archive_get_group(&archive_database,
                          (job.ordinal+total-1)%total,&reply.previous);
                    (void)hub_archive_get_group(&archive_database,
                          (job.ordinal+1)%total,&reply.next);
                }
            } else if(job.kind==ARCHIVE_AI_LAST) {
                reply.found=hub_archive_last_digest(&archive_database,&reply.digest);
            } else {
                reply.found=hub_archive_get_record(&archive_database,
                                                  job.app,job.ordinal,&reply.record);
            }
            archive_error=archive_database.failed;
            (void)xQueueOverwrite(archive_replies,&reply);
        }
    }
}
static bool uuid_is(const esp_bt_uuid_t *u,const uint8_t *v) {
    return u->len==ESP_UUID_LEN_128 && memcmp(u->uuid.uuid128,v,16)==0;
}
static void item_event(uint8_t type,const item_t *item) {
    if((type==0 || type==3) && item) {
        /* A source event is committed before waiting for detailed attributes.
         * A withdrawn alert may leave only this metadata snapshot. */
        archive_capture(item,type==0?HUB_ARCHIVE_SOURCE:HUB_ARCHIVE_PREVIEW);
        received_count++;
    }
    /* Type 1 = ANCS notification removed; deliberate no-op.
     * Type 2 = BLE session ended; archive is intentionally preserved. */
}
static void reset_session(void) {
    ready=false; paired=false; connected=false;
    requesting=false; pending_head=pending_tail=pending_count=0; stage=0;
    start_handle=end_handle=notification_handle=data_handle=control_handle=0;
    notify_cccd=data_cccd=0;
    hub_decoder_begin(&decoder,0);
    /* Do NOT clear or delete previously captured notifications. */
}
static const char *category_name(int category) {
    static const char *const labels[]={
      "Other","Call","Missed call","Voicemail","Social","Calendar",
      "Email","News","Health","Business","Location","Entertainment"
    };
    return (category>=0 && category<12)?labels[category]:"Notification";
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
    new_label(root,"NOTIFY ARCHIVE",12,7,190,&lv_font_montserrat_20,0xF1F8FF);
    top_battery=new_label(root,"--%",200,9,38,&lv_font_montserrat_14,0x7EE6B3);
    top_status=new_label(root,"Starting archive",12,39,216,&lv_font_montserrat_14,0xFFBB70);
    box(root,9,73,222,217);
    page_no=new_label(root,"APPLICATIONS",20,83,200,&lv_font_montserrat_14,0x7EE6B3);
    app_name=new_label(root,"Loading...",20,114,198,&readable_font,0xA6C4FF);
    title_text=new_label(root,"",20,153,198,&readable_font,0xFFFFFF);
    body_text=new_label(root,"",20,205,196,&readable_font,0xCFDBE9);
    help_text=new_label(root,"UP/DOWN:SELECT  OK:OPEN",9,297,222,
                        &lv_font_montserrat_14,0x96A9BA);
    lv_screen_load(root);
}
static const char *app_display(const char *bundle) {
    if(!strcmp(bundle,"com.tencent.xin")) return "WeChat";
    if(!strcmp(bundle,"com.tencent.mqq")) return "QQ";
    if(!strcmp(bundle,"com.apple.MobileSMS")) return "Messages";
    if(!strcmp(bundle,"com.apple.mobilemail")) return "Mail";
    if(!strcmp(bundle,"net.whatsapp.WhatsApp")) return "WhatsApp";
    if(!strcmp(bundle,"ph.telegra.Telegraph")) return "Telegram";
    return bundle[0]?bundle:"Unresolved";
}
/* UTF-8 codepoint pagination avoids chopping Han characters in half. */
static void digest_text_page(const char *source,uint16_t page,
                             char *out,size_t max) {
    if(!out || max==0) return;
    out[0]=0;
    if(!source || !*source) return;
    size_t skip=(size_t)page*60u;
    const unsigned char *p=(const unsigned char *)source;
    while(*p && skip) {
        size_t n=(*p<0x80)?1:(*p<0xE0)?2:(*p<0xF0)?3:4;
        for(size_t i=0;i<n&&*p;i++) p++;
        skip--;
    }
    size_t count=0,length=0;
    while(*p && count<60) {
        size_t n=(*p<0x80)?1:(*p<0xE0)?2:(*p<0xF0)?3:4;
        if(length+n>=max) break;
        for(size_t i=0;i<n&&*p;i++) out[length++]=(char)*p++;
        count++;
    }
    out[length]=0;
}
static uint16_t digest_pages(const char *source) {
    if(!source || !*source) return 1;
    uint32_t count=0;
    const unsigned char *p=(const unsigned char *)source;
    while(*p) {
        size_t n=(*p<0x80)?1:(*p<0xE0)?2:(*p<0xF0)?3:4;
        for(size_t i=0;i<n&&*p;i++) p++;
        count++;
    }
    return (uint16_t)((count+59)/60);
}
static void render(void) {
    int battery=bsp_battery_soc();
    if(battery<0) lv_label_set_text(top_battery,"--%");
    else lv_label_set_text_fmt(top_battery,"%d%%",battery);
    const char *status=archive_error?"ARCHIVE ERROR (READ ONLY)":
        archive_full?"ARCHIVE FULL - KEEP OLD":
        archive_dropped?"ARCHIVE DROPPED ALERT":
        !archive_loaded?"INITIALIZING":
        !ready?"WAITING ANCS":"IPHONE CONNECTED";
    if(archive_loaded && !archive_error && !archive_full &&
       !archive_dropped && !paired)
        lv_label_set_text_fmt(top_status,"PAIR CODE %06lu",(unsigned long)passkey);
    else lv_label_set_text(top_status,status);
    lv_obj_set_style_text_color(top_status,lv_color_hex(
        archive_error||archive_full||archive_dropped?0xFF8B87:
        ready?0x7EE6B3:0xFFBB70),0);
    if(view_mode==VIEW_DIGEST) {
        lv_obj_set_style_text_color(title_text,lv_color_hex(0x7EE6B3),0);
        uint16_t pages=latest_digest_ready?digest_pages(latest_digest.summary):1;
        if(digest_page>=pages) digest_page=pages-1;
        lv_label_set_text_fmt(page_no,"AI SUMMARY  %u / %u",
                              (unsigned)digest_page+1,(unsigned)pages);
        lv_label_set_text(app_name,latest_digest_ready?
                          "SAVED DIGEST":"NO SUMMARY YET");
        lv_label_set_text_fmt(title_text,latest_digest_ready?
                             "Batch to #%lu":"Set up gateway via USB",
                             (unsigned long)latest_digest.processed_through);
        static char fragment[256];
        digest_text_page(latest_digest_ready?latest_digest.summary:"",
                         digest_page,fragment,sizeof(fragment));
        lv_label_set_text(body_text,latest_digest_ready?
             fragment:(!ai_configured?"AI NOT CONFIGURED":
                       !ai_enabled?"AI DISABLED IN ADMIN":
                       ai_busy?"SUMMARIZING...":"WAITING FOR INTERVAL"));
        lv_label_set_text(help_text,"UP/DOWN:PAGE  HOLD OK:BACK");
    }else if(view_mode==VIEW_GROUPS) {
        lv_label_set_text_fmt(page_no,"APPLICATIONS %d / %lu",
             visible_group_count?group_cursor+1:0,(unsigned long)visible_group_count);
        if(!visible_group_count) {
            lv_label_set_text(app_name,"NO ARCHIVED APPS");
            lv_label_set_text(title_text,"No saved notifications yet");
            lv_label_set_text(body_text,"Connect iPhone to begin archiving.");
        }else {
            lv_label_set_text_fmt(app_name,"  %.17s  (%lu)",
                app_display(previous_group.app),(unsigned long)previous_group.count);
            lv_label_set_text_fmt(title_text,"> %.17s  (%lu)",
                app_display(selected_app),(unsigned long)visible_record_count);
            lv_label_set_text_fmt(body_text,"  %.17s  (%lu)",
                app_display(next_group.app),(unsigned long)next_group.count);
        }
        lv_obj_set_style_text_color(title_text,lv_color_hex(0x7EE6B3),0);
        lv_label_set_text(help_text,"UP/DOWN:APPS  OK:OPEN");
    }else if(view_mode==VIEW_LIST) {
        lv_obj_set_style_text_color(title_text,lv_color_hex(0xFFFFFF),0);
        lv_label_set_text_fmt(page_no,"HISTORY %d / %lu",
            visible_record_count?record_cursor+1:0,(unsigned long)visible_record_count);
        lv_label_set_text(app_name,app_display(selected_app));
        lv_label_set_text(title_text,selected_record_valid?
            selected_record.title:"No saved preview");
        lv_label_set_text(body_text,selected_record_valid?
            (selected_record.body[0]?selected_record.body:"(Only notification header captured)"):"");
        lv_label_set_text(help_text,"UP/DOWN:ITEM  OK:READ");
    }else {
        lv_obj_set_style_text_color(title_text,lv_color_hex(0xFFFFFF),0);
        lv_label_set_text_fmt(page_no,"SAVED SNAPSHOT #%lu",
             (unsigned long)selected_record.sequence);
        lv_label_set_text(app_name,app_display(selected_app));
        lv_label_set_text(title_text,selected_record.title);
        lv_label_set_text(body_text,selected_record.body[0]?
            selected_record.body:"(No preview was provided by iOS)");
        lv_label_set_text(help_text,"HOLD OK:BACK");
    }
}
static void refresh(lv_timer_t *timer) {
    (void)timer;
    archive_reply_t reply;
    while(xQueueReceive(archive_replies,&reply,0)==pdTRUE) {
        if(reply.kind==ARCHIVE_GROUP && view_mode==VIEW_GROUPS) {
            visible_group_count=reply.total_groups;
            if(reply.found) {
                snprintf(selected_app,sizeof(selected_app),"%s",reply.group.app);
                visible_record_count=reply.group.count;
                previous_group=reply.previous;
                next_group=reply.next;
            }else {
                selected_app[0]=0;visible_record_count=0;
                memset(&previous_group,0,sizeof(previous_group));
                memset(&next_group,0,sizeof(next_group));
            }
        } else if(reply.kind==ARCHIVE_AI_LAST) {
            latest_digest_ready=reply.found;
            digest_page=0;
            if(reply.found) latest_digest=reply.digest;
        } else if(reply.kind==ARCHIVE_RECORD && view_mode==VIEW_LIST) {
            selected_record_valid=reply.found;
            if(reply.found) selected_record=reply.record;
        }
    }
    button_t btn;
    while(xQueueReceive(button_queue,&btn,0)==pdTRUE) {
        if(btn.ev==BSP_BTN_LONG && btn.btn==BSP_BTN_UP) {
            if(view_mode==VIEW_GROUPS) {
                view_mode=VIEW_DIGEST;
                archive_job_t latest={.kind=ARCHIVE_AI_LAST};
                (void)post_job(&latest);
            } else if(view_mode==VIEW_DIGEST) {
                view_mode=VIEW_GROUPS;request_group(group_cursor);
            }
        } else if(btn.ev==BSP_BTN_LONG && btn.btn==BSP_BTN_DOWN &&
                  view_mode==VIEW_DIGEST) {
            ai_run_now=true; /* Back-end still must explicitly enable sending. */
        } else if(btn.ev==BSP_BTN_LONG && btn.btn==BSP_BTN_OK) {
            if(view_mode==VIEW_DIGEST) {
                view_mode=VIEW_GROUPS;request_group(group_cursor);
            } else if(view_mode==VIEW_DETAIL) view_mode=VIEW_LIST;
            else if(view_mode==VIEW_LIST) {
                view_mode=VIEW_GROUPS;
                request_group(group_cursor);
            }
        }else if(btn.ev==BSP_BTN_CLICK) {
            if(btn.btn==BSP_BTN_OK) {
                if(view_mode==VIEW_GROUPS && visible_group_count) {
                    view_mode=VIEW_LIST; record_cursor=0;
                    selected_record_valid=false;request_record();
                } else if(view_mode==VIEW_LIST && selected_record_valid) {
                    view_mode=VIEW_DETAIL;
                }
            } else if(btn.btn==BSP_BTN_UP || btn.btn==BSP_BTN_DOWN) {
                int delta=btn.btn==BSP_BTN_DOWN?1:-1;
                if(view_mode==VIEW_DIGEST) {
                    int pages=digest_pages(latest_digest_ready?
                                          latest_digest.summary:"");
                    digest_page=(uint16_t)((digest_page+pages+delta)%pages);
                }
                if(view_mode==VIEW_GROUPS && visible_group_count) {
                    group_cursor=(group_cursor+(int)visible_group_count+delta) %
                                  (int)visible_group_count;
                    request_group(group_cursor);
                } else if(view_mode==VIEW_LIST && visible_record_count) {
                    record_cursor=(record_cursor+(int)visible_record_count+delta) %
                                   (int)visible_record_count;
                    selected_record_valid=false; request_record();
                }
            }
        }
    }
    int64_t now=esp_timer_get_time();
    if(view_mode==VIEW_GROUPS && archive_loaded && now>=next_summary_request) {
        request_group(group_cursor);
        next_summary_request=now+3000000;
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
    if(requesting) {
        if(pending_count==ANCS_REQUEST_BACKLOG) {
            archive_dropped=true; /* Preview couldn't be requested in time. */
            return;
        }
        pending_uids[pending_tail]=uid;
        pending_tail=(uint8_t)((pending_tail+1)%ANCS_REQUEST_BACKLOG);
        pending_count++;
        return;
    }
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
    if(pending_count && ready) {
        uint32_t uid=pending_uids[pending_head];
        pending_head=(uint8_t)((pending_head+1)%ANCS_REQUEST_BACKLOG);
        pending_count--;
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
                item_event(3,&item);
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
    archive_jobs=xQueueCreate(24,sizeof(archive_job_t));
    archive_replies=xQueueCreate(1,sizeof(archive_reply_t));
    ai_requests=xQueueCreate(1,sizeof(ai_archive_req_t));
    ai_replies=xQueueCreate(1,sizeof(ai_archive_resp_t));
    if(!button_queue || !archive_jobs || !archive_replies ||
       !ai_requests || !ai_replies) {ESP_LOGE(TAG,"Queues unavailable");return;}
    (void)bsp_button_init(on_button,NULL);
    bsp_display_backlight(80);
    if(!bsp_lvgl_lock(1000)) {ESP_LOGE(TAG,"UI lock unavailable");return;}
    build_ui();
    (void)lv_timer_create(refresh,350,NULL);
    bsp_lvgl_unlock();

    esp_err_t err=nvs_flash_init();
    if(err!=ESP_OK) {ESP_LOGE(TAG,"NVS unavailable: %s",esp_err_to_name(err));return;}
    if(xTaskCreate(archive_task,"archive_worker",7168,NULL,4,NULL)!=pdPASS) {
        archive_error=true;
        ESP_LOGE(TAG,"Could not start archive worker");
        return;
    }
    extern void hub_ai_provision_start(void);
    hub_ai_provision_start();
    if(xTaskCreate(ai_task,"ai_worker",8192,NULL,3,NULL)!=pdPASS)
        ESP_LOGW(TAG,"AI worker not started");
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
