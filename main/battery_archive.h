#pragma once
#include "battery_care.h"
/* Portable file-backed engine. Caller serializes access; no LVGL/ESP dependency.
 * Metadata+asset+audit use a durable replayable intent and atomic file replacement. */
bool battery_archive_open(const char *root);
bool battery_archive_page(bat_db_t *page,care_info_t *info,uint32_t after,const char *query,int status,int64_t now);
bool battery_archive_get(uint32_t id,care_asset_t *asset);
bat_result_t battery_archive_upsert(const bat_asset_t *asset,int64_t remind_at,unsigned charge_minutes,uint32_t revision,int64_t now,int tz);
bat_result_t battery_archive_action(uint32_t id,bat_action_t action,uint32_t revision,int64_t now,int tz);
bool battery_archive_snooze(int64_t now);
bool battery_archive_settings(unsigned volume,unsigned quiet_start,unsigned quiet_end,bool muted);
bool battery_archive_migrate(const bat_db_t *legacy);
#ifdef BAT_ARCHIVE_TEST
extern int battery_archive_fail_stage;
#endif

uint32_t battery_archive_previous(uint32_t after,int status,int64_t now);

int battery_archive_history(bat_event_t *events,unsigned capacity,uint32_t after,uint32_t revision);

bool battery_archive_ready(void);

bool battery_archive_migrated(void);
