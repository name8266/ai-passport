#pragma once
#include "battery_model.h"
#include "battery_care.h"
#include "esp_err.h"
/* Worker/HTTP-only API; owns model+filesystem mutex. Never called under LVGL lock. */
esp_err_t battery_store_init(void);
bool battery_store_writable(void);
bool battery_store_snapshot(bat_db_t *out);
bool battery_store_page(bat_db_t *out,care_info_t *info,uint32_t after,const char *query,int status);
bool battery_store_get(uint32_t id,care_asset_t *out);
esp_err_t battery_store_upsert(const bat_asset_t *asset,uint32_t revision,bat_result_t *result);
esp_err_t battery_store_upsert_care(const bat_asset_t *asset,int64_t remind_at,unsigned charge_minutes,uint32_t revision,bat_result_t *result);
esp_err_t battery_store_action(uint32_t id,bat_action_t action,uint32_t revision,bat_result_t *result);
bool battery_store_snooze(void);
bool battery_store_settings(unsigned volume,unsigned quiet_start,unsigned quiet_end,bool muted);
bool battery_time_sync(int64_t epoch,int timezone_minutes);
int64_t battery_time_now(int *timezone_minutes);

uint32_t battery_store_previous(uint32_t after,int status);

int battery_store_history(bat_event_t *events,unsigned capacity,uint32_t after,uint32_t revision);
