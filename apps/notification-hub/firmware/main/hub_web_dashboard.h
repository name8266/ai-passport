#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "hub_archive.h"

typedef struct {
    bool success;
    bool has_older;
    bool has_summary;
    bool phone_connected;
    bool wifi_online;
    bool ai_enabled;
    bool ai_busy;
    bool ai_failed;
    bool archive_full;
    bool archive_error;
    bool archive_dropped;
    uint8_t theme;
    uint8_t brightness;
    uint16_t retention_days;
    uint8_t record_count;
    uint32_t next_cursor;
    uint32_t archive_rows;
    uint32_t now_epoch;
    hub_archive_record_t records[HUB_ARCHIVE_WEB_PAGE_SIZE];
    hub_ai_digest_t summary;
} hub_web_dashboard_t;

typedef bool (*hub_web_dashboard_provider_t)(uint32_t before_slot,
                                             hub_web_dashboard_t *out);

void hub_ai_web_set_dashboard_provider(hub_web_dashboard_provider_t provider);
bool hub_app_request_ai_summary(void);

/* Text-only mirror of current LVGL labels; never a camera/LCD pixel capture. */
typedef struct {
    bool dark;
    char heading[64], status[96], battery[16], page[192];
    char app[192], title[256], body[1200], help[192];
} hub_web_screen_t;
bool hub_app_screen_snapshot(hub_web_screen_t *out);
bool hub_app_remote_button(unsigned action);
