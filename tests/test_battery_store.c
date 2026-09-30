#define _POSIX_C_SOURCE 200809L
#include "battery_store.h"
#include "freertos/semphr.h"
#include "esp_littlefs.h"
#include "esp_partition.h"
#include "nvs.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

const char *battery_test_root;
static bat_db_t legacy;
static bool exists,mutex_fail,nonblank,mount_fail;
static unsigned formats;
static esp_err_t init_error;
static int64_t monotonic;
static size_t free_space=100000;
SemaphoreHandle_t xSemaphoreCreateMutex(void) { return &legacy; }
int xSemaphoreTake(SemaphoreHandle_t m,int t) { (void)m;(void)t;return !mutex_fail; }
void xSemaphoreGive(SemaphoreHandle_t m) { (void)m; }
int64_t esp_timer_get_time(void) { return monotonic; }
esp_err_t nvs_flash_init(void) { return init_error; }
esp_err_t nvs_open(const char *n,int m,nvs_handle_t *h) { assert(m==NVS_READONLY);assert(!strcmp(n,"battery_v1"));*h=1;return exists ? ESP_OK : ESP_ERR_NVS_NOT_FOUND; }
esp_err_t nvs_get_blob(nvs_handle_t h,const char *k,void *out,size_t *size) {
    (void)h;assert(!strcmp(k,"assets"));assert(*size==sizeof(legacy));memcpy(out,&legacy,*size);return ESP_OK;
}
void nvs_close(nvs_handle_t h) { (void)h; }
const esp_partition_t *esp_partition_find_first(int type,int sub,const char *label) {
    static const esp_partition_t p={.size=1024};assert(type==1 && sub==255 && !strcmp(label,"assets"));return &p;
}
esp_err_t esp_partition_read(const esp_partition_t *p,size_t offset,void *out,size_t size) {
    (void)p;(void)offset;memset(out,nonblank ? 0x42 : 0xff,size);return ESP_OK;
}
esp_err_t esp_vfs_littlefs_register(const esp_vfs_littlefs_conf_t *config) {
    assert(!strcmp(config->base_path,battery_test_root));
    if(config->format_if_mount_failed){++formats;return ESP_OK;}return mount_fail ? ESP_FAIL : ESP_OK;
}
esp_err_t esp_littlefs_info(const char *label,size_t *total,size_t *used) {
    assert(!strcmp(label,"assets"));*total=0x4f0000;*used=*total-free_space;return ESP_OK;
}
int main(int argc,char **argv) {
    assert(argc==3);battery_test_root=argv[1];const char *mode=argv[2];
    if(!strcmp(mode,"corrupt")) {
        exists=true;bat_init(&legacy);legacy.checksum^=1;
        assert(battery_store_init()==ESP_ERR_INVALID_STATE && !battery_store_writable());assert(formats==0);
        puts("Corrupt legacy NVS: locked, source preserved");return 0;
    }
    if(!strcmp(mode,"init-fail")) {
        init_error=ESP_ERR_NVS_NO_FREE_PAGES;assert(battery_store_init()==init_error && !battery_store_writable());assert(formats==0);
        puts("NVS initialization failure: no erase");return 0;
    }
    if(!strcmp(mode,"mount-fail")) {
        mount_fail=nonblank=true;assert(battery_store_init()==ESP_FAIL && !battery_store_writable());assert(formats==0);
        puts("Nonblank filesystem mount failure: no format");return 0;
    }
    if(!strcmp(mode,"blank"))mount_fail=true;
    if(!strcmp(mode,"migration")) {
        exists=true;bat_init(&legacy);bat_asset_t a={.capacity_mah=2000,.soc=80,.health=98};strcpy(a.name,"Legacy");assert(bat_upsert(&legacy,&a,0,0)==BAT_OK);
    }
    assert(battery_store_init()==ESP_OK && battery_store_writable());
    assert(formats==(!strcmp(mode,"blank") ? 1u : 0u));
    bat_db_t page;care_info_t info;assert(battery_store_page(&page,&info,0,"",-1));
    assert(info.total==(exists ? 1u : 0u));if(exists)assert(!strcmp(page.assets[0].name,"Legacy") && bat_db_valid(&legacy));
    bat_asset_t a={.capacity_mah=3000,.soc=18,.health=98};strcpy(a.name,"Battery");bat_result_t result;
    for(unsigned i=0;i<40;++i) {
        assert(battery_store_upsert(&a,info.revision,&result)==ESP_OK && result==BAT_OK);assert(battery_store_page(&page,&info,0,"",-1));
    }
    assert(info.total==(exists ? 41u : 40u) && page.count==16 && info.due_count==40);
    assert(battery_store_action(page.assets[0].id,BAT_CHARGE,info.revision-1,&result)==ESP_OK && result==BAT_CONFLICT);
    mutex_fail=true;assert(!battery_store_snapshot(&page));assert(!battery_time_sync(BAT_MIN_EPOCH,480));mutex_fail=false;
    assert(battery_time_now(NULL)==0 && battery_time_sync(BAT_MIN_EPOCH,480));monotonic=5000000;assert(battery_time_now(NULL)==BAT_MIN_EPOCH+5);
    free_space=100;assert(battery_store_upsert(&a,info.revision,&result)==ESP_OK && result==BAT_FULL);
    assert(battery_store_page(&page,&info,0,"",-1) && info.total==(exists ? 41u : 40u));
    free_space=100000;assert(battery_store_settings(30,23,7,true));assert(battery_store_snooze());
    assert(battery_store_page(&page,&info,0,"",-1) && info.pet.muted && info.pet.volume==30 && info.pet.snooze_until==BAT_MIN_EPOCH+905);
    bat_event_t events[16];assert(battery_store_history(events,16,0,info.revision)==16);
    puts("Battery store: PASS (migration, 40 assets, locking, time, capacity reserve, settings and history)");
}
