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
#include "hub_backlog.h"

#define HUB_ARCHIVE_GROUP_LIMIT 96
#define HUB_ARCHIVE_MAX_RECORDS 11000
#define HUB_ARCHIVE_DIGEST_BYTES (512u*1024u)
#ifndef HUB_ARCHIVE_PATH
#define HUB_ARCHIVE_PATH "/archive/alerts.dat"
#endif
#define HUB_ARCHIVE_MAGIC 0x48425541u
#define HUB_ARCHIVE_VERSION 1u
#define HUB_ARCHIVE_TIME_EPOCH 1u
#define HUB_ARCHIVE_WEB_PAGE_SIZE 8u

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
    uint32_t elapsed_seconds; /* Unix time when reserved marks EPOCH; else legacy uptime */
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
    wl_handle_t wl;
    FILE *file;
    bool mounted, full, failed, volume_mounted;
    uint32_t session;
    uint32_t next_sequence;
    uint32_t rows;
    uint32_t usable_bytes;
    uint16_t group_count;
    hub_archive_group_t groups[HUB_ARCHIVE_GROUP_LIMIT];
    /* 14-bit journal slot plus 18-bit fingerprint, verified against Flash.
     * Covers 512 queued UIDs and the one active attribute request. */
    uint32_t pending[HUB_BACKLOG_CAPACITY+1];
    uint8_t superseded[(HUB_ARCHIVE_MAX_RECORDS + 7) / 8];
    uint16_t pending_cursor;
    /* One navigation position, not an in-RAM index of every record. */
    char browse_app[HUB_APP_BYTES];
    uint32_t browse_rows, browse_ordinal, browse_slot;
    bool browse_valid;
} hub_archive_t;

/* Never format a nonblank partition even if its filesystem is corrupt. */
bool hub_archive_open(hub_archive_t *db, uint32_t boot_session);
/* Append-only capture; never alter previously committed record bytes. */
bool hub_archive_capture(hub_archive_t *db, const hub_archive_record_t *input);
/* Expensive Flash reads run in the worker task only; ordinals are newest first. */
bool hub_archive_get_group(const hub_archive_t *db, uint32_t ordinal,
                           hub_archive_group_t *out);
/* Empty app selects the combined newest-first feed; cache holds one record. */
bool hub_archive_get_record(hub_archive_t *db, const char *app,
                            uint32_t newest_ordinal, hub_archive_record_t *out);
bool hub_archive_get_recent(hub_archive_t *db,uint32_t before_slot,uint8_t limit,
                            hub_archive_record_t *out,uint8_t *count,
                            uint32_t *next_cursor,bool *has_older);
/* Expire only already-processed, timestamped records. Pending AI inputs stay. */
bool hub_archive_expire(hub_archive_t *db,uint32_t now_epoch,
                        uint16_t retention_days,uint32_t *removed_rows);

/* All filesystem helpers run exclusively inside the archive worker. */
bool hub_archive_collect_since(hub_archive_t *db,uint32_t cursor,uint8_t max_count,
                               hub_ai_batch_t *out);
bool hub_archive_save_digest(hub_archive_t *db,const hub_ai_digest_t *digest);
bool hub_archive_last_digest(hub_archive_t *db,hub_ai_digest_t *out);
/* Restore a durable summary checkpoint when power fails before NVS commit. */
bool hub_archive_reconcile_cursor(hub_archive_t *db);
/* Paired checksummed snapshots: state survives resets without growing logs. */
uint32_t hub_archive_digest_revision(hub_archive_t *db);
bool hub_archive_hide_digest(hub_archive_t *db);
bool hub_archive_show_digest(hub_archive_t *db);
bool hub_archive_ack_digest(hub_archive_t *db);
/* Compare revision and unique task fingerprint before completing one item. */
bool hub_archive_complete_task(hub_archive_t *db,uint32_t fingerprint,
                               uint32_t expected_revision);
/* Physically reclaim handled snapshots only after the new digest is durable. */
bool hub_archive_prune_processed(hub_archive_t *db,uint32_t *removed_rows);

/* Explicit destructive recovery, only for an unavailable archive. */
bool hub_archive_initialize(hub_archive_t *db, bool confirmed);
/* Explicitly erase a healthy archive after a separately confirmed request. */
bool hub_archive_clear(hub_archive_t *db, bool confirmed);
