#pragma once
/*
 * Flash-backed append-only ANCS capture journal.
 * The archive owns its FATFS handle on one worker task, never in BLE/LVGL callbacks.
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include "wear_levelling.h"
#include "hub_protocol.h"
#include "hub_ai.h"

#define HUB_ARCHIVE_GROUP_LIMIT 96
#define HUB_ARCHIVE_MAX_RECORDS 11000
#define HUB_ARCHIVE_PATH "/archive/alerts.dat"
#define HUB_ARCHIVE_MAGIC 0x48425541u
#define HUB_ARCHIVE_VERSION 1u

typedef enum {
    HUB_ARCHIVE_SOURCE = 1, /* durable metadata immediately after ANCS source */
    HUB_ARCHIVE_PREVIEW = 2 /* ANCS attrs, including app / title / limited body */
} hub_archive_kind_t;

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t bytes;
    uint32_t sequence;
    uint32_t session;
    uint32_t uid;
    uint32_t elapsed_seconds; /* boot uptime, NOT a real wall-clock timestamp */
    uint8_t kind;
    uint8_t category;
    uint8_t obsolete; /* v1 reserved (always zero); supersedes tracked in RAM */
    uint8_t reserved;
    char app[HUB_APP_BYTES];
    char title[HUB_TITLE_BYTES];
    char body[HUB_BODY_BYTES];
    uint32_t checksum;
} hub_archive_record_t;

typedef struct {
    char app[HUB_APP_BYTES];
    uint32_t count;
} hub_archive_group_t;

typedef struct {
    uint32_t uid;
    uint32_t session;
    uint32_t offset;
    bool active;
} hub_archive_pending_t;

typedef struct {
    wl_handle_t wl;
    FILE *file;
    bool mounted, full, failed;
    uint32_t session;
    uint32_t next_sequence;
    uint32_t rows;
    uint32_t usable_bytes;
    uint16_t group_count;
    hub_archive_group_t groups[HUB_ARCHIVE_GROUP_LIMIT];
    hub_archive_pending_t pending[64];
    uint8_t superseded[(HUB_ARCHIVE_MAX_RECORDS + 7) / 8];
    uint8_t pending_cursor;
} hub_archive_t;

/* Never format a nonblank partition even if its filesystem is corrupt. */
bool hub_archive_open(hub_archive_t *db, uint32_t boot_session);
/* Append-only capture; never alter previously committed record bytes. */
bool hub_archive_capture(hub_archive_t *db, const hub_archive_record_t *input);
/* Expensive Flash reads run in the worker task only; ordinals are newest first. */
bool hub_archive_get_group(const hub_archive_t *db, uint32_t ordinal,
                           hub_archive_group_t *out);
bool hub_archive_get_record(hub_archive_t *db, const char *app,
                            uint32_t newest_ordinal, hub_archive_record_t *out);

/* All filesystem helpers run exclusively inside the archive worker. */
bool hub_archive_collect_since(hub_archive_t *db,uint32_t cursor,uint8_t max_count,
                               hub_ai_batch_t *out);
bool hub_archive_save_digest(hub_archive_t *db,const hub_ai_digest_t *digest);
bool hub_archive_last_digest(hub_archive_t *db,hub_ai_digest_t *out);
