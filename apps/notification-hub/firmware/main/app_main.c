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
#include <time.h>
#include "esp_log.h"
#include "esp_system.h"
#include "esp_random.h"
#include "esp_heap_caps.h"
#include "hub_ui.h"
#include "hub_sound.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_gap_ble_api.h"
#include "esp_gattc_api.h"
#include "esp_gatt_common_api.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "bsp_i2c.h"
#include "bsp_display.h"
#include "bsp_button.h"
#include "bsp_battery.h"
#include "lvgl.h"
#include "hub_protocol.h"
#include "hub_archive.h"
#include "hub_backlog.h"
#include "hub_ai.h"
#include "hub_web_dashboard.h"
#include "hub_app_catalog.h"

static const char *TAG="notify_hub";
static void hub_log_memory(const char *phase) {
    ESP_LOGI(TAG,"%s: internal heap=%u largest=%u",phase,
        (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT),
        (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT));
}
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
typedef struct { bsp_btn_t btn; bsp_btn_ev_t ev; uint32_t remote_id; } button_t;
static QueueHandle_t remote_button_replies;
static uint32_t remote_button_id;
typedef enum { ARCHIVE_SAVE=1, ARCHIVE_GROUP=2, ARCHIVE_RECORD=3, ARCHIVE_AI_LAST=4, ARCHIVE_INITIALIZE=5, ARCHIVE_CLEAR=6 } archive_job_kind_t;
typedef struct {
    archive_job_kind_t kind;
    char app[HUB_APP_BYTES];
    uint32_t ordinal;
    uint32_t generation;
} archive_job_t;
typedef struct {
    archive_job_kind_t kind;
    bool found;
    uint32_t generation;
    uint32_t ordinal;
    uint32_t total_groups;
    uint32_t total_records;
    /* These replies are mutually exclusive. Do not retain/copy all payloads. */
    union {
        struct {
            hub_archive_group_t group;
            hub_archive_group_t previous;
            hub_archive_group_t next;
        };
        hub_archive_record_t record;
        hub_ai_digest_t digest;
    };
} archive_reply_t;
typedef enum { AI_LOAD_BATCH=1, AI_STORE_DIGEST=2 } ai_archive_kind_t;
typedef struct {
    ai_archive_kind_t kind;
    uint32_t id;
    uint32_t after_sequence;
    uint8_t max_records;
    hub_ai_digest_t digest;
} ai_archive_req_t;
typedef struct {
    bool success;
    ai_archive_kind_t kind;
    hub_ai_batch_t batch;
    hub_ai_digest_t prior;
    bool has_prior;
} ai_archive_resp_t;
typedef struct {uint32_t id,before_slot;} web_archive_request_t;

static QueueHandle_t button_queue, archive_jobs, archive_captures, archive_control, archive_replies;
static QueueHandle_t web_archive_requests,web_archive_replies;
static SemaphoreHandle_t web_archive_mutex;
static uint32_t web_archive_id;
static hub_web_dashboard_t web_archive_snapshot;
/* Only one AI operation is outstanding. Completion carries its ID; the archive
 * worker owns this mailbox until publishing, then AI reads until its next send. */
static ai_archive_resp_t ai_response;
static uint32_t ai_operation_id;
static QueueHandle_t ai_requests, ai_replies;
static volatile bool ai_configured, ai_online, ai_enabled, ai_busy;
static volatile bool ai_failed;
static volatile bool ai_run_now;
static hub_ai_digest_t latest_digest;
static bool latest_digest_ready;

static hub_archive_t archive_database; /* only archive_task accesses fields */
static volatile bool archive_loaded, archive_error, archive_full, archive_dropped;
static enum { VIEW_GROUPS=0, VIEW_LIST=1, VIEW_DETAIL=2, VIEW_DIGEST=3, VIEW_CONFIG=4 } view_mode=VIEW_LIST;
static int group_cursor, record_cursor;
static uint32_t ui_generation;
static bool group_pending, record_pending;
static uint32_t visible_group_count, visible_record_count;
static char selected_app[HUB_APP_BYTES];
static hub_archive_group_t previous_group, next_group;
static hub_archive_record_t selected_record;
static bool selected_record_valid;
static int64_t next_summary_request;
static int64_t next_retention_check;
static lv_obj_t *content_panel;
static lv_obj_t *top_status,*top_battery,*page_no,*app_name,*title_text,*body_text,*help_text;
static lv_font_t readable_font;
static lv_obj_t *header_title,*notice_card;
static const hub_ui_palette_t *palette;
static bool dark_theme;
static uint32_t passkey;
static volatile bool paired, ready, connected, ble_failed;
static volatile bool archive_initializing;
static volatile uint8_t archive_clear_status;
static esp_gatt_if_t gatt_interface=ESP_GATT_IF_NONE;
static uint16_t conn_id, start_handle, end_handle, notification_handle, data_handle;
static uint16_t control_handle, notify_cccd, data_cccd;
static esp_bd_addr_t phone_address;
static hub_decoder_t decoder;

bool hub_ai_archive_clear_request(void) {
    if(!archive_control || archive_clear_status!=0) return false;
    uint8_t command=ARCHIVE_CLEAR;
    archive_clear_status=1;
    archive_initializing=true;
    if(xQueueSend(archive_control,&command,0)!=pdTRUE) {
        archive_initializing=false;
        archive_clear_status=0;
        return false;
    }
    return true;
}
int hub_ai_archive_clear_status(void) {return archive_clear_status;}
bool hub_app_request_ai_summary(void) {
    if(!ai_enabled || ai_busy) return false;
    ai_run_now=true;
    return true;
}
static bool provide_web_dashboard(uint32_t before_slot,hub_web_dashboard_t *out) {
    if(!out || !web_archive_mutex || !web_archive_requests || !web_archive_replies ||
       xSemaphoreTake(web_archive_mutex,pdMS_TO_TICKS(5000))!=pdTRUE) return false;
    web_archive_request_t request={.id=++web_archive_id,.before_slot=before_slot};
    bool ok=xQueueOverwrite(web_archive_requests,&request)==pdTRUE;
    int64_t deadline=esp_timer_get_time()+5000000;
    uint32_t completed=0;
    while(ok && esp_timer_get_time()<deadline) {
        int64_t remaining=deadline-esp_timer_get_time();
        uint32_t wait_ms=(uint32_t)((remaining+999)/1000);
        if(xQueueReceive(web_archive_replies,&completed,pdMS_TO_TICKS(wait_ms))!=pdTRUE) break;
        /* Discard a late completion after an earlier HTTP timeout. */
        if(completed==request.id) {ok=web_archive_snapshot.success;break;}
    }
    ok=ok && completed==request.id;
    if(ok) *out=web_archive_snapshot;
    xSemaphoreGive(web_archive_mutex);
    return ok;
}
static bool requesting;
static hub_backlog_t pending_details;
static uint32_t capture_session;
static bool mtu_ready, discovery_started;
static portMUX_TYPE deadline_lock=portMUX_INITIALIZER_UNLOCKED;
static int64_t request_deadline;
static esp_timer_handle_t request_watchdog;
static void set_request_deadline(int64_t deadline) {
    portENTER_CRITICAL(&deadline_lock);
    request_deadline=deadline;
    portEXIT_CRITICAL(&deadline_lock);
}
static void check_request_timeout(void *unused) {
    (void)unused;
    esp_bd_addr_t address;
    bool expired=false;
    portENTER_CRITICAL(&deadline_lock);
    if(request_deadline && esp_timer_get_time()>=request_deadline) {
        request_deadline=0;
        memcpy(address,phone_address,sizeof(address));
        expired=true;
    }
    portEXIT_CRITICAL(&deadline_lock);
    if(expired) {
        archive_dropped=true;
        /* ANCS fragments have no framing to safely skip an incomplete reply.
         * Reconnect instead of interpreting late fragments as the next UID. */
        (void)esp_ble_gap_disconnect(address);
    }
}
static int stage;
static uint32_t received_count;

static bool post_job(const archive_job_t *job) {
    if(job->kind==ARCHIVE_INITIALIZE) {
        uint8_t command=ARCHIVE_INITIALIZE;
        return archive_control && xQueueSend(archive_control,&command,0)==pdTRUE;
    }
    /* Coalesce browse requests; they cannot occupy durable capture slots. */
    return archive_jobs && xQueueOverwrite(archive_jobs,job)==pdTRUE;
}
static void request_group(int index) {
    archive_job_t request={.kind=ARCHIVE_GROUP,.ordinal=(uint32_t)index};
    request.generation=++ui_generation;
    group_pending=true;
    (void)post_job(&request);
}
static void request_record(void) {
    archive_job_t request={.kind=ARCHIVE_RECORD,.ordinal=(uint32_t)record_cursor};
    request.generation=++ui_generation;
    snprintf(request.app,sizeof(request.app),"%s",selected_app);
    record_pending=post_job(&request);
}
static void archive_capture(const item_t *source,hub_archive_kind_t kind) {
    hub_archive_record_t record={0};
    record.uid=source->uid;
    record.session=capture_session;
    record.category=source->category;
    record.kind=kind;
    uint32_t epoch=hub_ai_current_epoch();
    if(epoch) {record.elapsed_seconds=epoch;record.reserved=HUB_ARCHIVE_TIME_EPOCH;}
    snprintf(record.app,sizeof(record.app),"%s",
             kind==HUB_ARCHIVE_SOURCE?"Unresolved":source->app);
    snprintf(record.title,sizeof(record.title),"%s",source->title);
    snprintf(record.body,sizeof(record.body),"%s",source->body);
    /* Reserve two slots for serialized detail replies during metadata bursts. */
    if(!archive_captures ||
       (kind==HUB_ARCHIVE_SOURCE && uxQueueSpacesAvailable(archive_captures)<=2) ||
       xQueueSend(archive_captures,&record,0)!=pdTRUE)
        archive_dropped=true;
}
/* A summary request asks for a page of unprocessed archival snapshots.
 * The archive task alone owns the FAT journal and digest file.
 */
static void handle_ai_archive_request(const ai_archive_req_t *request) {
    ai_archive_resp_t *reply=&ai_response;
    memset(reply,0,sizeof(*reply));reply->kind=request->kind;
    if(request->kind==AI_LOAD_BATCH) {
        reply->success=hub_archive_collect_since(&archive_database,
                    request->after_sequence,request->max_records,&reply->batch);
        if(reply->success && reply->batch.count && reply->batch.day_tag &&
           hub_archive_last_digest(&archive_database,&reply->prior) &&
           reply->prior.day_tag==reply->batch.day_tag &&
           reply->prior.processed_through<=request->after_sequence)
            reply->has_prior=true;
        /* Metadata carries no body to summarize. A later detail is appended at
         * a new sequence, so skipping a metadata-only tail cannot lose it. */
        if(reply->success && !reply->batch.count &&
           reply->batch.through_sequence>request->after_sequence)
            reply->success=hub_ai_advance_cursor(reply->batch.through_sequence);
    }else if(request->kind==AI_STORE_DIGEST) {
        /* Commit summary bytes before advancing the NVS checkpoint. */
        reply->success=hub_archive_save_digest(&archive_database,&request->digest);
        if(reply->success)
            reply->success=hub_ai_advance_cursor(request->digest.processed_through);
        if(reply->success) {
            archive_job_t ui_request={.kind=ARCHIVE_AI_LAST};
            (void)post_job(&ui_request);
        }
    }
    if(ai_replies) (void)xQueueOverwrite(ai_replies,&request->id);
}


static bool receive_ai_completion(uint32_t expected,uint32_t timeout_ms);
static void ai_task(void *arg) {
    (void)arg;
    static hub_ai_connection_t conn;
    ai_configured=hub_ai_load_connection(&conn);
    /* Start SoftAP + local admin even before user's home Wi-Fi is known.
     * The Passport, not an external Mac/NAS gateway, owns everything. */
    if(!hub_ai_connect_wifi(&conn)) {
        ESP_LOGW(TAG,"Embedded Wi-Fi admin startup failed");
        ai_failed=true;vTaskDelete(NULL);return;
    }
    hub_log_memory("after Wi-Fi/admin");
    static hub_ai_settings_t settings;
    /* Web changes reboot the application. An unused TLS worker should not
     * retain its 8 KiB stack when AI is disabled, leaving room for audio DMA. */
    bool settings_loaded=hub_ai_read_settings(&settings);
    ai_enabled=settings_loaded && settings.enabled;
    if(settings_loaded && !settings.enabled) {
        ai_enabled=false;vTaskDelete(NULL);return;
    }
    int64_t next_config=0, next_run=0;
    while(true) {
        int64_t now=esp_timer_get_time()/1000000;
        if(now>=next_config) {
            bool was_enabled=settings.enabled;
            if(hub_ai_fetch_settings(&conn,&settings)) {
                ai_online=true;
                if(settings.enabled && !was_enabled)
                    next_run=now+settings.interval_minutes*60;
                ai_enabled=settings.enabled;
            }else ai_online=false;
            next_config=now+60;
        }
        if(archive_loaded && !archive_error && ai_online && settings.enabled && (ai_run_now || now>=next_run)) {
            ai_run_now=false;
            ai_busy=true;
            ai_failed=false;
            uint32_t cursor=hub_ai_read_cursor();
            int processed_batches=0;
            bool pending_more=false;
            for(int batch_index=0;batch_index<4;batch_index++) {
                static ai_archive_req_t operation;
                operation=(ai_archive_req_t){.kind=AI_LOAD_BATCH,
                    .id=++ai_operation_id,.after_sequence=cursor,
                    .max_records=(uint8_t)(settings.max_records<HUB_AI_MAX_BATCH?
                                         settings.max_records:HUB_AI_MAX_BATCH)};
                if(xQueueSend(ai_requests,&operation,pdMS_TO_TICKS(1000))!=pdTRUE)
                    {ai_failed=true;break;}
                ai_archive_resp_t *response=&ai_response;
                if(!receive_ai_completion(operation.id,5000) ||
                   response->kind!=AI_LOAD_BATCH || !response->success) {
                    ai_failed=true;break;
                }
                if(response->batch.count==0) {pending_more=false;break;}
                static hub_ai_digest_t result;
                if(!hub_ai_summarize_context(&conn,&settings,&response->batch,
                       response->has_prior?&response->prior:NULL,&result)) {
                    ai_failed=true;break;
                }
                /* xQueueSend copies the complete request; reuse the LOAD
                 * scratch buffer for STORE after its reply has arrived. */
                operation=(ai_archive_req_t){.kind=AI_STORE_DIGEST,.id=++ai_operation_id,.digest=result};
                if(xQueueSend(ai_requests,&operation,pdMS_TO_TICKS(1000))!=pdTRUE ||
                   !receive_ai_completion(operation.id,8000) ||
                   response->kind!=AI_STORE_DIGEST || !response->success) {
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
            ESP_LOGI(TAG,"AI window: batches=%d failed=%d backlog=%d capture_queue=%u",
                     processed_batches,ai_failed,pending_more,
                     (unsigned)uxQueueMessagesWaiting(archive_captures));
            hub_log_memory("after AI window");
            ai_busy=false;
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
static bool receive_ai_completion(uint32_t expected,uint32_t timeout_ms) {
    int64_t end=esp_timer_get_time()+(int64_t)timeout_ms*1000;
    uint32_t id;
    while(esp_timer_get_time()<end) {
        int64_t remaining=end-esp_timer_get_time();
        if(remaining<=0) return false;
        uint32_t left=(uint32_t)((remaining+999)/1000);
        if(xQueueReceive(ai_replies,&id,pdMS_TO_TICKS(left))!=pdTRUE) return false;
        if(id==expected) return true;
    }
    return false;
}
/* Exclusively owns FATFS access; filesystem work never runs inside BLE callback. */
static void archive_task(void *arg) {
    (void)arg;
    bool ok=hub_archive_open(&archive_database,esp_random());
    /* If a user cleared the archive outside the web flow, its NVS AI cursor
     * can outlive the records. An empty archive starts again at sequence 1. */
    if(ok && archive_database.rows==0 && hub_ai_read_cursor()!=0) {
        if(hub_ai_reset_cursor())
            ESP_LOGI(TAG,"Empty archive detected; stale AI cursor reset");
        else
            ESP_LOGE(TAG,"Empty archive but stale AI cursor could not be reset");
    }
    archive_error=!ok;
    archive_loaded=ok;
    archive_full=archive_database.full;
    if(ok) {
        static archive_reply_t start;
        start.kind=ARCHIVE_GROUP;
        uint32_t total=0;
        hub_archive_group_t g;
        while(hub_archive_get_group(&archive_database,total,&g)) total++;
        start.total_groups=total;
        start.found=hub_archive_get_group(&archive_database,0,&start.group);
        (void)xQueueOverwrite(archive_replies,&start);
    }
    archive_job_t load_latest={.kind=ARCHIVE_AI_LAST};
    (void)post_job(&load_latest);
    static archive_job_t job;
    while(true) {
        static hub_archive_record_t captured;
        for(int i=0;i<8 && xQueueReceive(archive_captures,&captured,0)==pdTRUE;i++) {
            if(!ok || !hub_archive_capture(&archive_database,&captured)) {
                archive_full=archive_database.full;archive_error=archive_database.failed;
                archive_dropped=true;
            }
        }
        static ai_archive_req_t request;
        if(xQueueReceive(ai_requests,&request,0)==pdTRUE) {
            if(ok) handle_ai_archive_request(&request);
            else {
                ai_response=(ai_archive_resp_t){.kind=request.kind,.success=false};
                (void)xQueueOverwrite(ai_replies,&request.id);
            }
        }
        static web_archive_request_t web_request;
        if(xQueueReceive(web_archive_requests,&web_request,0)==pdTRUE) {
            memset(&web_archive_snapshot,0,sizeof(web_archive_snapshot));
            web_archive_snapshot.success=ok && hub_archive_get_recent(
                &archive_database,web_request.before_slot,HUB_ARCHIVE_WEB_PAGE_SIZE,
                web_archive_snapshot.records,&web_archive_snapshot.record_count,
                &web_archive_snapshot.next_cursor,&web_archive_snapshot.has_older);
            web_archive_snapshot.archive_rows=ok?archive_database.rows:0;
            web_archive_snapshot.phone_connected=ready;
            web_archive_snapshot.wifi_online=hub_ai_is_online();
            web_archive_snapshot.ai_enabled=ai_enabled;
            web_archive_snapshot.ai_busy=ai_busy;
            web_archive_snapshot.ai_failed=ai_failed;
            web_archive_snapshot.archive_full=archive_full;
            web_archive_snapshot.archive_error=archive_error;
            web_archive_snapshot.archive_dropped=archive_dropped;
            web_archive_snapshot.theme=hub_ai_read_theme();
            web_archive_snapshot.brightness=hub_ai_read_brightness();
            web_archive_snapshot.now_epoch=hub_ai_current_epoch();
            static hub_ai_settings_t web_settings;
            if(hub_ai_read_settings(&web_settings))
                web_archive_snapshot.retention_days=web_settings.retention_days;
            if(ok) web_archive_snapshot.has_summary=
                hub_archive_last_digest(&archive_database,&web_archive_snapshot.summary);
            (void)xQueueOverwrite(web_archive_replies,&web_request.id);
        }
        uint8_t command;
        int64_t monotonic_now=esp_timer_get_time();
        if(ok && archive_loaded && !archive_initializing && !ai_busy &&
           monotonic_now>=next_retention_check) {
            if(uxQueueMessagesWaiting(archive_captures)==0) {
                uint32_t epoch=hub_ai_current_epoch();
                if(epoch) {
                    static hub_ai_settings_t retention_settings;
                    uint32_t removed=0;
                    if(hub_ai_read_settings(&retention_settings) &&
                       !hub_archive_expire(&archive_database,epoch,
                           retention_settings.retention_days,&removed)) {
                        if(archive_database.failed) archive_error=true;
                        else ESP_LOGW(TAG,"Retention compaction deferred; archive left intact");
                    }else if(removed) {
                        archive_full=archive_database.full;
                        archive_job_t latest={.kind=ARCHIVE_AI_LAST};
                        (void)post_job(&latest);
                        if(view_mode==VIEW_GROUPS) request_group(group_cursor);
                        ESP_LOGI(TAG,"Retention cleanup completed (%lu rows)",
                                 (unsigned long)removed);
                    }
                    next_retention_check=monotonic_now+6LL*60*60*1000000;
                }else next_retention_check=monotonic_now+60LL*1000000;
            }else next_retention_check=monotonic_now+5LL*1000000;
        }
        if(xQueueReceive(archive_control,&command,0)==pdTRUE)
            job=(archive_job_t){.kind=(archive_job_kind_t)command};
        else if(xQueueReceive(archive_jobs,&job,pdMS_TO_TICKS(10))!=pdTRUE) continue;
        if(job.kind==ARCHIVE_INITIALIZE) {
            /* Only an explicit confirmation on the initialization screen posts this job. */
            if(!ok && !archive_database.mounted) {
                ok=hub_ai_reset_cursor() && hub_archive_initialize(&archive_database,true);
                archive_loaded=ok;archive_error=!ok;archive_full=false;archive_dropped=false;
            }
            archive_initializing=false;
            continue;
        }
        if(job.kind==ARCHIVE_CLEAR) {
            static hub_archive_record_t discard;
            while(xQueueReceive(archive_captures,&discard,0)==pdTRUE) {}
            bool cleared=hub_ai_reset_cursor();
            if(cleared) {
                cleared=archive_database.mounted?
                    hub_archive_clear(&archive_database,true):
                    hub_archive_initialize(&archive_database,true);
            }
            ok=cleared;
            archive_loaded=cleared;
            archive_error=!cleared;
            archive_full=false;
            archive_dropped=false;
            if(cleared) {
                static archive_reply_t empty;
                memset(&empty,0,sizeof(empty));
                empty.kind=ARCHIVE_GROUP;
                (void)xQueueOverwrite(archive_replies,&empty);
            }
            archive_initializing=false;
            archive_clear_status=cleared?2:3;
            continue;
        }
        if(!ok) continue;
        {
            static archive_reply_t reply;
            memset(&reply,0,sizeof(reply));
            reply.kind=job.kind;
            reply.generation=job.generation;
            reply.ordinal=job.ordinal;
            if(job.kind==ARCHIVE_GROUP) {
                hub_archive_group_t g;
                uint32_t total=0;
                while(hub_archive_get_group(&archive_database,total,&g)) total++;
                reply.total_groups=total;
                if(total) job.ordinal%=total;
                reply.ordinal=job.ordinal;
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
                hub_archive_group_t group;
                for(uint32_t i=0;hub_archive_get_group(&archive_database,i,&group);i++) {
                    if(!job.app[0] || strcmp(job.app,group.app)==0)
                        reply.total_records+=group.count;
                }
                if(reply.total_records && job.ordinal>=reply.total_records)
                    job.ordinal=reply.total_records-1;
                reply.ordinal=job.ordinal;
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
    if((type==0 || type==3) && item && !archive_initializing) {
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
    mtu_ready=false;discovery_started=false;
    set_request_deadline(0);
    requesting=false; pending_details=(hub_backlog_t){0}; stage=0;
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
    lv_obj_set_pos(label,x,y);
    lv_obj_set_width(label,width);
    lv_obj_set_style_text_font(label,font,0);
    hub_ui_set_text(label,str);
    lv_obj_set_style_text_color(label,lv_color_hex(color),0);
    lv_label_set_long_mode(label,LV_LABEL_LONG_WRAP);
    return label;
}
static void build_ui(void) {
    readable_font=notification_hub_16;
    readable_font.fallback=&lv_font_montserrat_14;
    if(!hub_ui_font_check(&readable_font)) ESP_LOGE(TAG,"UI glyph coverage failed");
    palette=hub_ui_palette(dark_theme);
    lv_obj_t *root=lv_obj_create(NULL);
    lv_obj_remove_flag(root,LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(root,lv_color_hex(palette->background),0);
    lv_obj_set_style_border_width(root,0,0);
    lv_obj_set_style_pad_all(root,0,0);
    new_label(root,"Notify Hub",16,9,145,&lv_font_montserrat_14,palette->secondary);
    top_battery=new_label(root,"--%",194,9,38,&lv_font_montserrat_14,palette->secondary);
    header_title=new_label(root,"通知中心",16,35,210,&notification_hub_24,palette->primary);
    top_status=new_label(root,"正在启动",16,69,210,&readable_font,palette->secondary);
    page_no=new_label(root,"应用分组",16,96,208,&readable_font,palette->secondary);
    notice_card=lv_obj_create(root);
    lv_obj_set_pos(notice_card,12,121);lv_obj_set_size(notice_card,216,158);
    lv_obj_set_style_bg_color(notice_card,lv_color_hex(palette->surface),0);
    lv_obj_set_style_radius(notice_card,22,0);
    lv_obj_set_style_border_width(notice_card,0,0);
    lv_obj_set_style_pad_all(notice_card,0,0);
    lv_obj_remove_flag(notice_card,LV_OBJ_FLAG_SCROLLABLE);
    content_panel=lv_obj_create(notice_card);
    lv_obj_set_pos(content_panel,12,10);lv_obj_set_size(content_panel,192,140);
    lv_obj_set_style_bg_opa(content_panel,LV_OPA_TRANSP,0);
    lv_obj_set_style_border_width(content_panel,0,0);lv_obj_set_style_pad_all(content_panel,0,0);
    lv_obj_set_scroll_dir(content_panel,LV_DIR_VER);
    app_name=new_label(content_panel,"",0,0,190,&readable_font,palette->secondary);
    title_text=new_label(content_panel,"",0,37,190,&readable_font,palette->accent);
    body_text=new_label(content_panel,"",0,77,190,&readable_font,palette->primary);
    help_text=new_label(root,"上下选择  确认查看",14,294,212,&readable_font,palette->secondary);
    lv_screen_load(root);
}
bool hub_app_remote_button(unsigned action) {
    if(action>5 || !button_queue || !remote_button_replies || archive_initializing) return false;
    /* Called only by the serial HTTP server task. Never wait in the LVGL task. */
    uint32_t id=++remote_button_id;
    if(!id) id=++remote_button_id;
    button_t item={.btn=(bsp_btn_t)(action%3),
        .ev=action<3?BSP_BTN_CLICK:BSP_BTN_LONG,.remote_id=id};
    if(xQueueSend(button_queue,&item,0)!=pdTRUE) return false;
    int64_t deadline=esp_timer_get_time()+1500000;
    while(esp_timer_get_time()<deadline) {
        uint32_t completed=0;
        if(xQueueReceive(remote_button_replies,&completed,pdMS_TO_TICKS(50))==pdTRUE &&
           completed==id) return true;
    }
    return false;
}
bool hub_app_screen_snapshot(hub_web_screen_t *out) {
    if(!out || !bsp_lvgl_lock(500)) return false;
    memset(out,0,sizeof(*out));
    bool ok=header_title && body_text;
    if(ok) {
        out->dark=dark_theme;
#define COPY_LABEL(field, label) do { \
    if(!lv_obj_has_flag(label,LV_OBJ_FLAG_HIDDEN)) \
        snprintf(out->field,sizeof(out->field),"%s",lv_label_get_text(label)); \
} while(0)
        COPY_LABEL(heading,header_title); COPY_LABEL(status,top_status);
        COPY_LABEL(battery,top_battery); COPY_LABEL(page,page_no);
        COPY_LABEL(app,app_name); COPY_LABEL(title,title_text);
        COPY_LABEL(body,body_text); COPY_LABEL(help,help_text);
#undef COPY_LABEL
    }
    bsp_lvgl_unlock();
    return ok;
}
static const char *app_display(const char *bundle) {
    return hub_catalog_display(bundle);
}
static void render(void) {
    static int last_view=-1;
    int battery=bsp_battery_soc();
    if(battery<0) hub_ui_set_text(top_battery,"--%");
    else hub_ui_set_text_fmt(top_battery,"%d%%",battery);
    const char *status=archive_initializing?"正在初始化":archive_error?"存档需要处理":
        archive_full?"存档已满":archive_dropped?"部分通知未保存":!archive_loaded?"正在加载":ready?"手机已连接":"等待手机连接";
    if(connected && !paired) hub_ui_set_text_fmt(top_status,"配对码 %06lu",(unsigned long)passkey);
    else hub_ui_set_text(top_status,status);
    lv_obj_set_style_text_color(top_status,lv_color_hex(
        archive_error || archive_dropped || hub_sound_error()?palette->danger:palette->secondary),0);
    const char *heading=view_mode==VIEW_CONFIG?"设置与连接":
        view_mode==VIEW_DIGEST?"智能摘要":
        view_mode==VIEW_DETAIL?"阅读通知":view_mode==VIEW_LIST && selected_app[0]?"应用通知":"通知中心";
    hub_ui_set_text(header_title,heading);
    bool reading=view_mode==VIEW_DETAIL || view_mode==VIEW_DIGEST || view_mode==VIEW_CONFIG || archive_initializing;
    if(last_view!=(int)view_mode) {lv_obj_scroll_to_y(content_panel,0,LV_ANIM_OFF);last_view=(int)view_mode;}
    lv_obj_set_flag(app_name,LV_OBJ_FLAG_HIDDEN,reading);
    lv_obj_set_flag(title_text,LV_OBJ_FLAG_HIDDEN,reading);
    lv_obj_set_pos(body_text,0,reading?0:77);
    lv_obj_set_height(app_name,31);lv_obj_set_height(title_text,37);
    lv_label_set_long_mode(app_name,LV_LABEL_LONG_DOT);lv_label_set_long_mode(title_text,LV_LABEL_LONG_DOT);
    lv_obj_set_height(body_text,reading?LV_SIZE_CONTENT:61);
    lv_label_set_long_mode(body_text,reading?LV_LABEL_LONG_WRAP:LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_color(title_text,lv_color_hex(palette->primary),0);
    if(archive_initializing) {
        hub_ui_set_text(page_no,"正在清空通知档案");
        hub_ui_set_text(body_text,"正在处理本机存档，请保持设备供电。完成后设备会自动重启。");
        hub_ui_set_text(help_text,"请稍候");
    }else if(view_mode==VIEW_CONFIG) {
        char ap[33]={0};
        bool ok=hub_ai_get_hotspot_ssid(ap,sizeof(ap));
        hub_ui_set_text(page_no,"免密码连接热点");
        hub_ui_set_text_fmt(body_text,"%s\n\n192.168.4.1\n打开即可管理，无需登录",ok?ap:"正在启动");
        hub_ui_set_text(help_text,"网页内设置  长按确认返回");
    }else if(view_mode==VIEW_DIGEST) {
        hub_ui_set_text(page_no,ai_busy?"正在总结":ai_failed?"生成失败，请重试":"长按下键重新总结");
        hub_ui_set_text(body_text,latest_digest_ready?latest_digest.summary:!ai_enabled?"智能摘要尚未开启。\n\n在网页设置中开启。":ai_failed?"摘要失败，请检查网络或存档。":"等待下一次摘要");
        hub_ui_set_text(help_text,"上下滚动  长按确认返回");
    }else if(view_mode==VIEW_GROUPS) {
        if(!visible_group_count) {
            hub_ui_set_text(page_no,"全部通知");
            hub_ui_set_text(app_name,"通知档案");
            hub_ui_set_text(title_text,"暂无通知");
            hub_ui_set_text(body_text,archive_error?"长按确认打开设置，\n可处理旧存档。":"连接手机，开始接收。 ");
        }else {
            hub_ui_set_text_fmt(page_no,"应用 %d / %lu",group_cursor+1,(unsigned long)visible_group_count);
            hub_ui_set_text(app_name,"通知档案");
            hub_ui_set_text_fmt(title_text,"%s · %s",hub_catalog_category(selected_app),app_display(selected_app));
            hub_ui_set_text_fmt(body_text,"%lu 条通知\n确认查看",(unsigned long)visible_record_count);
        }
        hub_ui_set_text(help_text,"长按:上看摘要/确认设置");
    }else if(view_mode==VIEW_LIST) {
        hub_ui_set_text_fmt(page_no,"第 %d / %lu 条",visible_record_count?record_cursor+1:0,(unsigned long)visible_record_count);
        bool compact=selected_record_valid && (!selected_record.title[0] ||
            !strcmp(selected_record.title,app_display(selected_record.app)));
        lv_obj_set_flag(title_text,LV_OBJ_FLAG_HIDDEN,compact);
        lv_obj_set_pos(body_text,0,compact?37:77);
        lv_obj_set_height(body_text,compact?101:61);
        hub_ui_set_text(app_name,selected_record_valid?app_display(selected_record.app):"全部通知");
        hub_ui_set_text(title_text,selected_record_valid?selected_record.title:archive_loaded && !record_pending?"暂无通知":"正在读取");
        hub_ui_set_text(body_text,selected_record_valid?selected_record.body:archive_loaded && !record_pending?"连接手机，开始接收。":"");
        hub_ui_set_text(help_text,"确认阅读  长按上键摘要");
    }else {
        hub_ui_set_text(page_no,app_display(selected_record.app));
        hub_ui_set_text_fmt(body_text,"%s\n\n%s",selected_record.title,selected_record.body[0]?selected_record.body:"手机未提供正文");
        hub_ui_set_text(help_text,"上下滚动  长按确认返回");
    }
}
static void refresh(lv_timer_t *timer) {
    (void)timer;
    archive_reply_t reply;
    while(xQueueReceive(archive_replies,&reply,0)==pdTRUE) {
        if(reply.kind!=ARCHIVE_AI_LAST && reply.generation!=ui_generation) continue;
        if(reply.kind==ARCHIVE_GROUP && view_mode==VIEW_GROUPS) {
            group_pending=false;
            group_cursor=(int)reply.ordinal;
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
            if(reply.found) latest_digest=reply.digest;
        } else if(reply.kind==ARCHIVE_RECORD && view_mode==VIEW_LIST) {
            record_pending=false;
            visible_record_count=reply.total_records;
            record_cursor=(int)reply.ordinal;
            selected_record_valid=reply.found;
            if(reply.found) selected_record=reply.record;
        }
    }
    button_t btn;
    static uint32_t completed_remote;
    while(xQueueReceive(button_queue,&btn,0)==pdTRUE) {
        if(archive_initializing) continue;
        if(btn.remote_id) completed_remote=btn.remote_id;
        if(btn.ev==BSP_BTN_LONG && btn.btn==BSP_BTN_DOWN &&
           (view_mode==VIEW_GROUPS || view_mode==VIEW_LIST)) {
            view_mode=VIEW_GROUPS;request_group(group_cursor);
        } else if(btn.ev==BSP_BTN_LONG && btn.btn==BSP_BTN_UP) {
            if(view_mode==VIEW_GROUPS || view_mode==VIEW_LIST) {
                view_mode=VIEW_DIGEST;
                archive_job_t latest={.kind=ARCHIVE_AI_LAST};
                (void)post_job(&latest);
            } else if(view_mode==VIEW_DIGEST) {
                view_mode=VIEW_LIST;selected_app[0]=0;record_cursor=0;
                selected_record_valid=false;request_record();
            }
        } else if(btn.ev==BSP_BTN_LONG && btn.btn==BSP_BTN_DOWN &&
                  view_mode==VIEW_DIGEST) {
            ai_run_now=true; /* Back-end still must explicitly enable sending. */
        } else if(btn.ev==BSP_BTN_LONG && btn.btn==BSP_BTN_OK) {
            if(view_mode==VIEW_GROUPS) {
                view_mode=VIEW_CONFIG;
            }else if(view_mode==VIEW_DIGEST || view_mode==VIEW_CONFIG) {
                view_mode=VIEW_LIST;selected_app[0]=0;record_cursor=0;
                selected_record_valid=false;request_record();
            } else if(view_mode==VIEW_DETAIL) view_mode=VIEW_LIST;
            else if(view_mode==VIEW_LIST) {
                if(!selected_app[0]) view_mode=VIEW_CONFIG;
                else {view_mode=VIEW_GROUPS;request_group(group_cursor);}
            }
        }else if(btn.ev==BSP_BTN_CLICK) {
            if(btn.btn==BSP_BTN_OK) {
                if(view_mode==VIEW_GROUPS && visible_group_count && !group_pending) {
                    view_mode=VIEW_LIST; record_cursor=0;
                    selected_record_valid=false;request_record();
                } else if(view_mode==VIEW_LIST && selected_record_valid) {
                    view_mode=VIEW_DETAIL;
                }
            } else if(btn.btn==BSP_BTN_UP || btn.btn==BSP_BTN_DOWN) {
                int delta=btn.btn==BSP_BTN_DOWN?1:-1;
                if(view_mode==VIEW_DIGEST || view_mode==VIEW_DETAIL || view_mode==VIEW_CONFIG) {
                    lv_obj_scroll_by(content_panel,0,-delta*60,LV_ANIM_OFF);
                }
                if(view_mode==VIEW_GROUPS && visible_group_count) {
                    group_cursor=(group_cursor+(int)visible_group_count+delta) %
                                  (int)visible_group_count;
                    request_group(group_cursor);
                } else if(view_mode==VIEW_LIST && visible_record_count) {
                    int next=record_cursor+delta;
                    if(next<0 || (uint32_t)next>=visible_record_count) continue;
                    record_cursor=next;
                    selected_record_valid=false; request_record();
                }
            }
        }
    }
    int64_t now=esp_timer_get_time();
    if(archive_loaded && now>=next_summary_request) {
        if(view_mode==VIEW_GROUPS) request_group(group_cursor);
        else if(view_mode==VIEW_LIST && (!selected_record_valid || record_cursor==0)) request_record();
        else if(view_mode==VIEW_DIGEST && !latest_digest_ready) {
            archive_job_t latest={.kind=ARCHIVE_AI_LAST};
            (void)post_job(&latest);
        }
        next_summary_request=now+3000000;
    }
    render();
    bool content_pending=(view_mode==VIEW_GROUPS && group_pending) ||
                         (view_mode==VIEW_LIST && record_pending);
    if(completed_remote && remote_button_replies && !content_pending) {
        (void)xQueueOverwrite(remote_button_replies,&completed_remote);
        completed_remote=0;
    }
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
        if(!hub_backlog_push(&pending_details,uid))
            archive_dropped=true;
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
        set_request_deadline(esp_timer_get_time()+15000000);
        hub_decoder_begin(&decoder,uid);
    }
}
static void request_next(void) {
    requesting=false;
    set_request_deadline(0);
    uint32_t uid;
    if(ready && hub_backlog_pop(&pending_details,&uid)) get_details(uid);
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
static void maybe_discover(void) {
    if(!paired || !mtu_ready || discovery_started) return;
    esp_bt_uuid_t target={.len=ESP_UUID_LEN_128};
    memcpy(target.uuid.uuid128,ancs_service,16);
    if(esp_ble_gattc_search_service(gatt_interface,conn_id,&target)==ESP_OK)
        discovery_started=true;
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
        reset_session();
        connected=true;capture_session=esp_random();
        portENTER_CRITICAL(&deadline_lock);
        memcpy(phone_address,p->connect.remote_bda,ESP_BD_ADDR_LEN);
        portEXIT_CRITICAL(&deadline_lock);
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
        /* ANCS also works with the default ATT MTU when negotiation fails. */
        mtu_ready=true;
        maybe_discover();
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
            hub_sound_notify(evt.event,evt.flags);
            item_t item={.uid=evt.uid,.category=evt.category};
            if(evt.event==2) {
                hub_backlog_remove(&pending_details,evt.uid);
                item_event(1,&item);
            }
            else {
                snprintf(item.app,sizeof(item.app),"%s",category_name(evt.category));
                strcpy(item.title,"新通知");
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
        ready=false;stage=0;requesting=false;
        set_request_deadline(0);
        pending_details=(hub_backlog_t){0};
        start_handle=end_handle=notification_handle=data_handle=control_handle=0;
        notify_cccd=data_cccd=0;
        discovery_started=false;
        hub_decoder_begin(&decoder,0);
        maybe_discover();
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
        else maybe_discover();
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
    esp_err_t err=nvs_flash_init();
    if(err!=ESP_OK) {ESP_LOGE(TAG,"NVS unavailable: %s",esp_err_to_name(err));return;}
    dark_theme=hub_ai_read_theme()==1;
    (void)bsp_battery_init();
    button_queue=xQueueCreate(8,sizeof(button_t));
    remote_button_replies=xQueueCreate(1,sizeof(uint32_t));
    archive_jobs=xQueueCreate(1,sizeof(archive_job_t));
    archive_captures=xQueueCreate(8,sizeof(hub_archive_record_t));
    archive_control=xQueueCreate(1,sizeof(uint8_t));
    archive_replies=xQueueCreate(1,sizeof(archive_reply_t));
    ai_requests=xQueueCreate(1,sizeof(ai_archive_req_t));
    ai_replies=xQueueCreate(1,sizeof(uint32_t));
    web_archive_requests=xQueueCreate(1,sizeof(web_archive_request_t));
    web_archive_replies=xQueueCreate(1,sizeof(uint32_t));
    web_archive_mutex=xSemaphoreCreateMutex();
    if(!button_queue || !remote_button_replies || !archive_jobs || !archive_captures || !archive_control || !archive_replies ||
       !ai_requests || !ai_replies || !web_archive_requests || !web_archive_replies ||
       !web_archive_mutex) {ESP_LOGE(TAG,"Queues unavailable");return;}
    hub_ai_web_set_dashboard_provider(provide_web_dashboard);
    (void)bsp_button_init(on_button,NULL);
    bsp_display_backlight(hub_ai_read_brightness());
    if(!bsp_lvgl_lock(1000)) {ESP_LOGE(TAG,"UI lock unavailable");return;}
    build_ui();
    (void)lv_timer_create(refresh,350,NULL);
    bsp_lvgl_unlock();

    hub_log_memory("before BLE");
    ESP_ERROR_CHECK(esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT));
    esp_bt_controller_config_t config=BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_bt_controller_init(&config));
    ESP_ERROR_CHECK(esp_bt_controller_enable(ESP_BT_MODE_BLE));
    ESP_ERROR_CHECK(esp_bluedroid_init());
    ESP_ERROR_CHECK(esp_bluedroid_enable());
    hub_log_memory("after BLE");

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
    const esp_timer_create_args_t watchdog={
        .callback=check_request_timeout,.name="ancs_timeout"};
    ESP_ERROR_CHECK(esp_timer_create(&watchdog,&request_watchdog));
    ESP_ERROR_CHECK(esp_timer_start_periodic(request_watchdog,1000000));
    ESP_ERROR_CHECK(esp_ble_gattc_app_register(0));
    if(xTaskCreate(archive_task,"archive_worker",4096,NULL,4,NULL)!=pdPASS) {
        archive_error=true;
        ESP_LOGE(TAG,"Could not start archive worker");
        return;
    }
    if(!hub_sound_start()) ESP_LOGW(TAG,"Sound worker not started");
    if(xTaskCreate(ai_task,"ai_worker",5120,NULL,3,NULL)!=pdPASS)
        ESP_LOGW(TAG,"AI worker not started");

}
