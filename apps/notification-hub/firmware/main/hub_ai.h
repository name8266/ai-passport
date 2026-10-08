#pragma once
/* All configuration, scheduling and the lightweight web admin run on Passport.
 * The ONLY remote service is the user-selected OpenAI-compatible AI API.
 * No NAS/Mac/Python gateway is required. No PSRAM is assumed.
 */
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "hub_protocol.h"

#define HUB_AI_MAX_BATCH 3
#define HUB_AI_SUMMARY_BYTES 1024
#define HUB_AI_DEFAULT_ENDPOINT "https://api.deepseek.com/chat/completions"
#define HUB_AI_DEFAULT_MODEL "deepseek-flash"

typedef struct {
    uint32_t after_sequence;
    uint32_t through_sequence;
    uint8_t count;
    struct {
        uint32_t sequence;
        char app[HUB_APP_BYTES];
        char title[HUB_TITLE_BYTES];
        char body[HUB_BODY_BYTES];
    } items[HUB_AI_MAX_BATCH];
} hub_ai_batch_t;

typedef struct {
    uint32_t processed_through;
    uint16_t included;
    char summary[HUB_AI_SUMMARY_BYTES];
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
    char endpoint[192];            /* HTTPS only */
    char model[64];
    char api_key[192];             /* never returned by HTTP admin page */
    char excluded_apps[192];       /* comma-separated ANCS app identifiers */
} hub_ai_settings_t;

/* Initializes WPA2 SoftAP admin on 192.168.4.1, and STA if provisioned.
 * The admin UI and timer are implemented on Passport, not a separate server. */
bool hub_ai_load_connection(hub_ai_connection_t *out);
bool hub_ai_connect_wifi(const hub_ai_connection_t *conn);
bool hub_ai_is_online(void);
bool hub_ai_get_access(char *ssid, size_t ssid_len,
                       char *ap_password, size_t pass_len,
                       char *admin_password, size_t admin_len);
bool hub_ai_read_settings(hub_ai_settings_t *out);
bool hub_ai_save_settings(const hub_ai_settings_t *settings,
                          const hub_ai_connection_t *wifi);
void hub_ai_web_start(void);
bool hub_ai_fetch_settings(const hub_ai_connection_t *conn,hub_ai_settings_t *out);
bool hub_ai_summarize(const hub_ai_connection_t *conn,
                      const hub_ai_batch_t *batch,hub_ai_digest_t *out);
uint32_t hub_ai_read_cursor(void);
bool hub_ai_advance_cursor(uint32_t value);
