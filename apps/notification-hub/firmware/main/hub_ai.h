#pragma once
/* Wi-Fi HTTPS bridge to user-managed AI gateway; no model keys on Passport. */
#include <stdbool.h>
#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "hub_protocol.h"

#define HUB_AI_MAX_BATCH 8
#define HUB_AI_SUMMARY_BYTES 1024
#define HUB_AI_DEVICE_NAME "passport-archive-01"

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
    char gateway[192]; /* Valid CA-trusted https://... URL, no trailing '/' */
    char token[129];   /* Shared device token; never send to AI provider. */
} hub_ai_connection_t;

typedef struct {
    bool enabled;
    int interval_minutes;
    int max_records;
} hub_ai_settings_t;

/* NVS namespace "hub_ai": ssid, pass, gateway, token, cursor.
 * Provisioning is deliberately separate; never hardcode secrets in Git.
 */
bool hub_ai_load_connection(hub_ai_connection_t *out);
bool hub_ai_save_connection(const hub_ai_connection_t *conn);
/* Starts Wi-Fi if configured. Must not be called in BLE callbacks. */
bool hub_ai_connect_wifi(const hub_ai_connection_t *conn);
/* HTTPS gateway calls, strict TLS verification (no certificate bypass). */
bool hub_ai_fetch_settings(const hub_ai_connection_t *conn,hub_ai_settings_t *out);
bool hub_ai_summarize(const hub_ai_connection_t *conn,
                      const hub_ai_batch_t *batch,hub_ai_digest_t *out);
uint32_t hub_ai_read_cursor(void);
bool hub_ai_advance_cursor(uint32_t value);
