#pragma once
/* All configuration, scheduling and the lightweight web admin run on Passport.
 * The ONLY remote service is the user-selected OpenAI-compatible AI API.
 * No NAS/Mac/Python gateway is required. No PSRAM is assumed.
 */
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "hub_protocol.h"
#include "hub_alert.h"

#define HUB_AI_MAX_BATCH 8
#define HUB_AI_TITLE_BYTES 64
#define HUB_AI_BODY_BYTES 96
#define HUB_AI_SUMMARY_BYTES 1024
#define HUB_AI_TASK_LIMIT 5
#define HUB_AI_TASK_BYTES 80
#define HUB_AI_SOURCE_BYTES 32
#define HUB_AI_DUE_BYTES 36
#define HUB_AI_TRIGGER_COUNT 3
/* First summary after acknowledgement waits for 3 previews. After that,
 * every new preview incrementally updates the same persisted digest. */
static inline bool hub_ai_batch_ready(uint8_t count,bool manual,bool has_digest) {
    return count>0 && (has_digest || manual || count>=HUB_AI_TRIGGER_COUNT);
}
static inline uint8_t hub_ai_batch_size(bool has_digest) {
    return has_digest?1u:HUB_AI_TRIGGER_COUNT;
}
#define HUB_AI_DEFAULT_ENDPOINT "https://api.deepseek.com/chat/completions"
#define HUB_AI_DEFAULT_MODEL "deepseek-flash"

typedef struct {
    uint32_t after_sequence;
    uint32_t through_sequence;
    uint8_t count;
    uint16_t day_tag; /* local calendar day; zero if no trusted clock */
    struct {
        uint32_t sequence;
        char app[HUB_APP_BYTES];
        char title[HUB_AI_TITLE_BYTES];
        char body[HUB_AI_BODY_BYTES];
        bool sensitive; /* classified using the full archive snapshot */
    } items[HUB_AI_MAX_BATCH];
} hub_ai_batch_t;

typedef struct {
    char task[HUB_AI_TASK_BYTES];
    char source[HUB_AI_SOURCE_BYTES];
    char due[HUB_AI_DUE_BYTES];
} hub_ai_task_t;

typedef struct {
    uint32_t processed_through;
    uint16_t included; /* cumulative notification count in this digest */
    uint16_t day_tag;
    char summary[HUB_AI_SUMMARY_BYTES];
    hub_ai_task_t tasks[HUB_AI_TASK_LIMIT];
    uint8_t task_count;
    bool hidden; /* Closing hides the card, never deletes its context. */
    bool cleared; /* Read acknowledgment retains the checkpoint, not the text. */
} hub_ai_digest_t;

typedef struct {
    bool configured;
    char ssid[33];
    char password[65];
} hub_ai_connection_t;

typedef struct {
    bool enabled;                  /* DEFAULT FALSE: explicit user opt-in */
    bool redact_sensitive;
    int interval_minutes;          /* 15 - 1440 */
    int max_records;               /* 1 - HUB_AI_MAX_BATCH */
    uint16_t retention_days;       /* 1 - 365, default 30 */
    uint8_t brightness;            /* 20 - 100 percent */
    char endpoint[192];            /* HTTPS only */
    char model[64];
    char api_key[192];             /* never returned by HTTP admin page */
    char excluded_apps[192];       /* comma-separated ANCS app identifiers */
    uint8_t theme;                 /* 0=light, 1=dark */
    hub_sound_config_t sound;
} hub_ai_settings_t;

/* Initializes open SoftAP admin on 192.168.4.1, and STA if provisioned.
 * The admin UI and timer are implemented on Passport, not a separate server. */
bool hub_ai_load_connection(hub_ai_connection_t *out);
bool hub_ai_connect_wifi(const hub_ai_connection_t *conn);
bool hub_ai_is_online(void);
/* Set on overflow only; ordinary informational updates keep working. */
bool hub_ai_last_capacity_issue(void;
bool hub_ai_wifi_started(void);
/* Only call from a dedicated short-lived task after admin inactivity. */
bool hub_ai_wifi_shutdown(void);
bool hub_ai_get_hotspot_ssid(char *ssid,size_t ssid_len);
bool hub_ai_read_settings(hub_ai_settings_t *out);
uint8_t hub_ai_read_theme(void);
uint8_t hub_ai_read_brightness(void);
uint32_t hub_ai_current_epoch(void); /* 0 until SNTP provides a valid clock */
bool hub_ai_save_settings(const hub_ai_settings_t *settings,
                          const hub_ai_connection_t *wifi);
bool hub_ai_web_start(void);
bool hub_ai_web_is_running(void);
int64_t hub_ai_web_idle_us(void);
/* Single AI worker temporarily releases the admin server's task/socket RAM.
 * SoftAP remains on; restart with hub_ai_web_start after HTTPS cleanup. */
bool hub_ai_web_pause(void);
bool hub_ai_fetch_settings(const hub_ai_connection_t *conn,hub_ai_settings_t *out);
bool hub_ai_summarize(const hub_ai_connection_t *conn,
                      const hub_ai_batch_t *batch,hub_ai_digest_t *out);
uint32_t hub_ai_read_cursor(void);
bool hub_ai_advance_cursor(uint32_t value);
/* Replace a checkpoint after archive compaction renumbers retained rows. */
bool hub_ai_set_cursor(uint32_t value);

bool hub_ai_reset_cursor(void);
/* Archive worker serializes the confirmed erase with BLE captures and Flash reads. */
bool hub_ai_archive_clear_request(void);
int hub_ai_archive_clear_status(void); /* 0=idle, 1=running, 2=complete, 3=failed */

/* Same direct HTTPS request, with caller-owned, nonpersistent credentials.
 * Must be serialized with hub_ai_summarize by the single AI worker. */
bool hub_ai_summarize_settings(const hub_ai_connection_t *conn,
                      const hub_ai_settings_t *configuration,
                      const hub_ai_batch_t *batch,hub_ai_digest_t *out);

/* Rolling context persists until acknowledged, including across calendar days. */
bool hub_ai_summarize_context(const hub_ai_connection_t *conn,
                 const hub_ai_settings_t *configuration,
                 const hub_ai_batch_t *batch,const hub_ai_digest_t *prior,
                 hub_ai_digest_t *out);
/* Compact local-day key stored in the legacy 16-bit digest version field.
 * Fixed China Standard Time (UTC+8); unknown timestamps return zero. */
static inline uint16_t hub_ai_day_tag(uint32_t epoch) {
    if(epoch<1700000000u) return 0;
    return (uint16_t)(0x8000u | (((epoch+28800u)/86400u)&0x7fffu));
}
