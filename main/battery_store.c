#include "battery_store.h"
#include "battery_archive.h"
#include "esp_littlefs.h"
#include "esp_partition.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "nvs.h"
#include "nvs_flash.h"
#include <stdlib.h>
#include <string.h>

#ifndef BAT_STORAGE_ROOT
#define BAT_STORAGE_ROOT "/assets"
#endif
static SemaphoreHandle_t s_mutex;
static bat_clock_t s_clock;
static bool s_ready;
static bool take(void) { return s_mutex && xSemaphoreTake(s_mutex,pdMS_TO_TICKS(3000))==pdTRUE; }
/* Only a wholly erased new partition may be formatted automatically. A mount
 * failure over any existing bytes fails closed, never destroys stored records. */
static bool erased(const esp_partition_t *p) {
    uint8_t data[256];
    for(size_t pos=0;pos<p->size;pos+=sizeof(data)) {
        size_t n=p->size-pos<sizeof(data) ? p->size-pos : sizeof(data);
        if(esp_partition_read(p,pos,data,n)!=ESP_OK)return false;
        for(size_t i=0;i<n;++i)if(data[i]!=0xff)return false;
    }return true;
}
esp_err_t battery_store_init(void) {
    s_mutex=xSemaphoreCreateMutex();if(!s_mutex)return ESP_ERR_NO_MEM;
    esp_err_t err=nvs_flash_init();if(err!=ESP_OK)return err;
    const esp_partition_t *part=esp_partition_find_first(ESP_PARTITION_TYPE_DATA,ESP_PARTITION_SUBTYPE_ANY,"assets");
    if(!part)return ESP_ERR_NOT_FOUND;
    esp_vfs_littlefs_conf_t config={.base_path=BAT_STORAGE_ROOT,.partition_label="assets",.format_if_mount_failed=false};
    err=esp_vfs_littlefs_register(&config);
    if(err!=ESP_OK) {
        if(!erased(part))return err;
        config.format_if_mount_failed=true;err=esp_vfs_littlefs_register(&config);
        if(err!=ESP_OK)return err;
    }
    if(!battery_archive_open(BAT_STORAGE_ROOT))return ESP_ERR_INVALID_STATE;
    /* V1 NVS stays untouched; the archive migration marker prevents resurrection
     * of subsequently deleted legacy assets on the next boot. */
    if(battery_archive_migrated()){s_ready=true;return ESP_OK;}
    nvs_handle_t old;
    err=nvs_open("battery_v1",NVS_READONLY,&old);
    if(err==ESP_OK) {
        bat_db_t *legacy=calloc(1,sizeof(*legacy));if(!legacy){nvs_close(old);return ESP_ERR_NO_MEM;}
        size_t size=sizeof(*legacy);err=nvs_get_blob(old,"assets",legacy,&size);nvs_close(old);
        if(err==ESP_OK && (size!=sizeof(*legacy) || !bat_db_valid(legacy))){free(legacy);return ESP_ERR_INVALID_STATE;}
        if(err==ESP_OK && !battery_archive_migrate(legacy)){free(legacy);return ESP_ERR_INVALID_STATE;}
        if(err!=ESP_OK && err!=ESP_ERR_NVS_NOT_FOUND){free(legacy);return err;}
        free(legacy);
    } else if(err==ESP_ERR_NVS_NOT_FOUND) {
        bat_db_t *empty=calloc(1,sizeof(*empty));if(!empty)return ESP_ERR_NO_MEM;bat_init(empty);
        bool ok=battery_archive_migrate(empty);free(empty);if(!ok)return ESP_ERR_INVALID_STATE;
    }
    if(err!=ESP_OK && err!=ESP_ERR_NVS_NOT_FOUND)return err;
    s_ready=true;return ESP_OK;
}
bool battery_store_writable(void) {
    if(!s_ready || !take())return false;
    bool ok=battery_archive_ready();xSemaphoreGive(s_mutex);return ok;
}
bool battery_store_page(bat_db_t *out,care_info_t *info,uint32_t after,const char *q,int status) {
    if(!s_ready || !take())return false;
    bool ok=battery_archive_page(out,info,after,q,status,bat_clock_now(&s_clock,esp_timer_get_time()/1000));
    int64_t now=bat_clock_now(&s_clock,esp_timer_get_time()/1000);
    if(now){int32_t day=(int32_t)((now+s_clock.timezone_minutes*60)/86400);
        if(info->pet.reward_day!=day)info->pet.daily_xp=0;
        if(info->pet.last_day<day-1)info->pet.streak=0;
    }
    size_t total=0,used=0;
    if(esp_littlefs_info("assets",&total,&used)==ESP_OK){info->storage_total=total;info->storage_used=used;}
    xSemaphoreGive(s_mutex);return ok;
}
bool battery_store_snapshot(bat_db_t *out) { care_info_t info;return battery_store_page(out,&info,0,"",-1); }
bool battery_store_get(uint32_t id,care_asset_t *out) {
    if(!s_ready || !take())return false;
    bool ok=battery_archive_get(id,out);xSemaphoreGive(s_mutex);return ok;
}
static bool space_available(void) {
    size_t total=0,used=0;return esp_littlefs_info("assets",&total,&used)==ESP_OK && total>used+32768;
}
esp_err_t battery_store_upsert_care(const bat_asset_t *a,int64_t remind,unsigned minutes,uint32_t r,bat_result_t *result) {
    *result=BAT_INVALID;if(!s_ready || !take())return ESP_ERR_INVALID_STATE;
    *result=space_available() ? battery_archive_upsert(a,remind,minutes,r,bat_clock_now(&s_clock,esp_timer_get_time()/1000),s_clock.timezone_minutes) : BAT_FULL;
    xSemaphoreGive(s_mutex);return ESP_OK;
}
esp_err_t battery_store_upsert(const bat_asset_t *a,uint32_t r,bat_result_t *result) {
    return battery_store_upsert_care(a,0,120,r,result);
}
esp_err_t battery_store_action(uint32_t id,bat_action_t action,uint32_t r,bat_result_t *result) {
    *result=BAT_INVALID;if(!s_ready || !take())return ESP_ERR_INVALID_STATE;
    /* Deletion may reclaim space even when the reserve threshold is reached. */
    *result=action==BAT_DELETE || space_available() ? battery_archive_action(id,action,r,bat_clock_now(&s_clock,esp_timer_get_time()/1000),s_clock.timezone_minutes) : BAT_FULL;
    xSemaphoreGive(s_mutex);return ESP_OK;
}
bool battery_store_snooze(void) {
    if(!s_ready || !take())return false;
    bool ok=battery_archive_snooze(bat_clock_now(&s_clock,esp_timer_get_time()/1000));xSemaphoreGive(s_mutex);return ok;
}
bool battery_store_settings(unsigned v,unsigned start,unsigned end,bool muted) {
    if(!s_ready || !take())return false;
    bool ok=battery_archive_settings(v,start,end,muted);xSemaphoreGive(s_mutex);return ok;
}
bool battery_time_sync(int64_t epoch,int tz) {
    if(!take())return false;
    bool ok=bat_clock_sync(&s_clock,epoch,tz,esp_timer_get_time()/1000);xSemaphoreGive(s_mutex);return ok;
}
int64_t battery_time_now(int *tz) {
    if(!take())return 0;
    int64_t epoch=bat_clock_now(&s_clock,esp_timer_get_time()/1000);if(tz)*tz=s_clock.timezone_minutes;xSemaphoreGive(s_mutex);return epoch;
}

uint32_t battery_store_previous(uint32_t after,int status) {
    if(!take())return 0;
    uint32_t result=battery_archive_previous(after,status,bat_clock_now(&s_clock,esp_timer_get_time()/1000));xSemaphoreGive(s_mutex);return result;
}

int battery_store_history(bat_event_t *events,unsigned capacity,uint32_t after,uint32_t revision) {
    if(!s_ready || !take())return -1;
    int n=battery_archive_history(events,capacity,after,revision);xSemaphoreGive(s_mutex);return n;
}
