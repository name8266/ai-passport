#include "battery_store.h"
#include "freertos/semphr.h"
#include "nvs.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static bat_db_t persisted,pending;
static bool exists,mutex_fail,set_fail,commit_fail;
static esp_err_t init_error;
static int64_t monotonic;
SemaphoreHandle_t xSemaphoreCreateMutex(void) { return &persisted; }
int xSemaphoreTake(SemaphoreHandle_t m,int t) { (void)m;(void)t; return !mutex_fail; }
void xSemaphoreGive(SemaphoreHandle_t m) { (void)m; }
int64_t esp_timer_get_time(void) { return monotonic; }
const char *esp_err_to_name(esp_err_t err) { (void)err;return "test"; }
esp_err_t nvs_flash_init(void) { return init_error; }
esp_err_t nvs_open(const char *n,int m,nvs_handle_t *h) { (void)m;assert(!strcmp(n,"battery_v1"));*h=1;return ESP_OK; }
esp_err_t nvs_get_blob(nvs_handle_t h,const char *k,void *out,size_t *size) {
    (void)h;assert(!strcmp(k,"assets"));if(!exists)return ESP_ERR_NVS_NOT_FOUND;
    assert(*size==sizeof(persisted));memcpy(out,&persisted,*size);return ESP_OK;
}
esp_err_t nvs_set_blob(nvs_handle_t h,const char *k,const void *data,size_t size) {
    (void)h;(void)k;assert(size==sizeof(pending));if(set_fail)return ESP_FAIL;
    memcpy(&pending,data,size);return ESP_OK;
}
esp_err_t nvs_commit(nvs_handle_t h) { (void)h;if(commit_fail)return ESP_FAIL;persisted=pending;exists=true;return ESP_OK; }
void nvs_close(nvs_handle_t h) { (void)h; }
int main(int argc,char **argv) {
    (void)argc;
    if (argv[1] && !strcmp(argv[1],"corrupt")) {
        exists=true;bat_init(&persisted);persisted.checksum^=1;
        assert(battery_store_init()==ESP_ERR_INVALID_STATE);assert(!battery_store_writable());
        assert(persisted.checksum!=pending.checksum);puts("Corrupt NVS: locked without erase");return 0;
    }
    if (argv[1] && !strcmp(argv[1],"init-fail")) {
        init_error=ESP_ERR_NVS_NO_FREE_PAGES;
        assert(battery_store_init()==init_error);assert(!battery_store_writable());
        puts("NVS init failure: no automatic erase");return 0;
    }
    assert(battery_store_init()==ESP_OK && battery_store_writable());
    bat_asset_t a={.capacity_mah=3000,.soc=80,.health=98};strcpy(a.name,"Battery");
    bat_result_t result;bat_db_t snapshot;
    set_fail=true;assert(battery_store_upsert(&a,0,&result)==ESP_FAIL);
    assert(battery_store_snapshot(&snapshot) && snapshot.count==0 && !exists);
    set_fail=false;commit_fail=true;assert(battery_store_upsert(&a,0,&result)==ESP_FAIL);
    assert(battery_store_snapshot(&snapshot) && snapshot.revision==0 && !exists);
    commit_fail=false;assert(battery_store_upsert(&a,0,&result)==ESP_OK && result==BAT_OK);
    assert(battery_store_snapshot(&snapshot) && snapshot.count==1 && bat_db_valid(&persisted));
    assert(battery_store_action(1,BAT_CHECKOUT,0,&result)==ESP_OK && result==BAT_CONFLICT);
    mutex_fail=true;assert(!battery_store_snapshot(&snapshot));
    assert(battery_store_action(1,BAT_CHECKOUT,1,&result)==ESP_ERR_TIMEOUT);
    mutex_fail=false;assert(battery_time_now(NULL)==0);
    assert(battery_time_sync(BAT_MIN_EPOCH,480));monotonic=5000000;
    assert(battery_time_now(NULL)==BAT_MIN_EPOCH+5);
    assert(battery_store_action(1,BAT_CHECKOUT,1,&result)==ESP_OK && result==BAT_OK);
    assert(persisted.events[1].epoch==BAT_MIN_EPOCH+5);
    puts("Battery store: PASS (write/commit rollback, conflicts, lock timeout, persisted event time)");return 0;
}
