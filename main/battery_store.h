#pragma once
#include "battery_model.h"
#include "esp_err.h"

/* Blocking worker/HTTP-task API. Owns model+NVS mutex; never called under LVGL lock. */
esp_err_t battery_store_init(void);
bool battery_store_writable(void);
bool battery_store_snapshot(bat_db_t *out);
esp_err_t battery_store_upsert(const bat_asset_t *asset, uint32_t revision, bat_result_t *result);
esp_err_t battery_store_action(uint32_t id, bat_action_t action, uint32_t revision, bat_result_t *result);
bool battery_time_sync(int64_t epoch, int timezone_minutes);
int64_t battery_time_now(int *timezone_minutes);
