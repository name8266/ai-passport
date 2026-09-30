#include "battery_store.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "nvs.h"
#include "nvs_flash.h"
#include <stdlib.h>
#include <string.h>

static bat_db_t s_db;
static bat_clock_t s_clock;
static SemaphoreHandle_t s_mutex;
static nvs_handle_t s_nvs;
static bool s_writable;

esp_err_t battery_store_init(void) {
    bat_init(&s_db);
    s_mutex=xSemaphoreCreateMutex();
    if (!s_mutex) return ESP_ERR_NO_MEM;
    esp_err_t err=nvs_flash_init();
    /* Never erase unrelated namespaces or recover corrupt data by silently resetting. */
    if (err==ESP_OK) err=nvs_open("battery_v1",NVS_READWRITE,&s_nvs);
    if (err!=ESP_OK) return err;
    size_t size=sizeof(s_db);
    err=nvs_get_blob(s_nvs,"assets",&s_db,&size);
    if (err==ESP_ERR_NVS_NOT_FOUND) { bat_init(&s_db); s_writable=true; return ESP_OK; }
    if (err!=ESP_OK || size!=sizeof(s_db) || !bat_db_valid(&s_db)) {
        bat_init(&s_db); nvs_close(s_nvs); s_nvs=0;
        return ESP_ERR_INVALID_STATE;
    }
    s_writable=true; return ESP_OK;
}
bool battery_store_writable(void) { return s_writable; }
bool battery_store_snapshot(bat_db_t *out) {
    if (!s_mutex || xSemaphoreTake(s_mutex,pdMS_TO_TICKS(1500))!=pdTRUE) return false;
    *out=s_db; xSemaphoreGive(s_mutex); return true;
}
static esp_err_t mutate(const bat_asset_t *asset,uint32_t id,bat_action_t action,
                        uint32_t revision,bat_result_t *result) {
    *result=BAT_INVALID;
    if (!s_writable) return ESP_ERR_INVALID_STATE;
    bat_db_t *candidate=malloc(sizeof(*candidate));
    if (!candidate) return ESP_ERR_NO_MEM;
    if (xSemaphoreTake(s_mutex,pdMS_TO_TICKS(2000))!=pdTRUE) { free(candidate); return ESP_ERR_TIMEOUT; }
    *candidate=s_db;
    int64_t epoch=bat_clock_now(&s_clock,esp_timer_get_time()/1000);
    *result=asset ? bat_upsert(candidate,asset,revision,epoch) : bat_action(candidate,id,action,revision,epoch);
    esp_err_t err=ESP_OK;
    if (*result==BAT_OK) {
        err=nvs_set_blob(s_nvs,"assets",candidate,sizeof(*candidate));
        if (err==ESP_OK) err=nvs_commit(s_nvs);
        /* Publish only after the persistent transaction succeeds. */
        if (err==ESP_OK) s_db=*candidate;
        else ESP_LOGE("battery_store","Save failed: %s",esp_err_to_name(err));
    }
    xSemaphoreGive(s_mutex); free(candidate); return err;
}
esp_err_t battery_store_upsert(const bat_asset_t *a,uint32_t r,bat_result_t *result) {
    return mutate(a,0,BAT_EDIT,r,result);
}
esp_err_t battery_store_action(uint32_t id,bat_action_t action,uint32_t r,bat_result_t *result) {
    return mutate(NULL,id,action,r,result);
}
bool battery_time_sync(int64_t epoch,int tz) {
    if (!s_mutex || xSemaphoreTake(s_mutex,pdMS_TO_TICKS(1000))!=pdTRUE) return false;
    bool ok=bat_clock_sync(&s_clock,epoch,tz,esp_timer_get_time()/1000);
    xSemaphoreGive(s_mutex); return ok;
}
int64_t battery_time_now(int *tz) {
    if (!s_mutex || xSemaphoreTake(s_mutex,pdMS_TO_TICKS(100))!=pdTRUE) return 0;
    int64_t epoch=bat_clock_now(&s_clock,esp_timer_get_time()/1000);
    if (tz) *tz=s_clock.timezone_minutes;
    xSemaphoreGive(s_mutex); return epoch;
}
