#include "app.h"
#include "nimble/nimble_port.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "freertos/semphr.h"
#include "esp_timer.h"
#include <string.h>

static TaskHandle_t host_handle;
static SemaphoreHandle_t host_done;
static bool stop_ack;
static uint8_t addr_type;
static int gap(struct ble_gap_event *e,void *arg) {
    (void)arg;ns_event_t msg={.generation=ns.radio_generation};
    if(e->type==BLE_GAP_EVENT_DISC) {
        msg.type=NSE_BLE;ns_ble_t *b=&msg.ble;
        memcpy(b->mac,e->disc.addr.val,6);b->addr_type=e->disc.addr.type;
        b->rssi=e->disc.rssi;b->event_type=e->disc.event_type;
        b->len=e->disc.length_data>31?31:e->disc.length_data;
        memcpy(b->data,e->disc.data,b->len);
        ns_ble_name(b->data,b->len,b->name,sizeof(b->name));
        b->last=esp_timer_get_time()/1000000;
    } else if(e->type==BLE_GAP_EVENT_DISC_COMPLETE)msg.type=NSE_BLE_READY;
    else return 0;
    if(xQueueSend(ns.events,&msg,0)!=pdTRUE) __atomic_fetch_add(&ns.ble_dropped,1,__ATOMIC_RELAXED);
    return 0;
}
static void reset(int reason) {
    ns_event_t e={.generation=ns.radio_generation,.type=NSE_BLE_RESET,.code=reason};(void)xQueueSend(ns.events,&e,0);
}
static void sync_host(void) {
    int rc=ble_hs_util_ensure_addr(0);
    if(!rc)rc=ble_hs_id_infer_auto(0,&addr_type);
    ns_event_t e={.generation=ns.radio_generation,.type=rc?NSE_BLE_RESET:NSE_BLE_READY,.code=rc};(void)xQueueSend(ns.events,&e,0);
}
static void host(void *arg) {
    (void)arg;nimble_port_run();xSemaphoreGive(host_done);
    for(;;)vTaskSuspend(NULL);
}
esp_err_t ns_ble_resume(void) {
    if(!ns.ble_initialized || ns.ble_stopping)return ESP_ERR_INVALID_STATE;
    if(ble_gap_disc_active())return ESP_OK;
    struct ble_gap_disc_params p={.passive=1,.filter_duplicates=1,.itvl=160,.window=80};
    int rc=ble_gap_disc(addr_type,5000,&p,gap,NULL);
    return rc?ESP_FAIL:ESP_OK;
}
esp_err_t ns_ble_start(void) {
    if(ns.ble_initialized)return ESP_OK;
    esp_err_t e=ns_radio_stop();if(e!=ESP_OK)return e;
    host_done=xSemaphoreCreateBinary();if(!host_done)return ESP_ERR_NO_MEM;
    e=nimble_port_init();
    if(e!=ESP_OK) { vSemaphoreDelete(host_done);host_done=NULL;return e; }
    ns.ble_initialized=true;ns.ble_stopping=false;stop_ack=false;
    ble_hs_cfg.reset_cb=reset;ble_hs_cfg.sync_cb=sync_host;
    if(xTaskCreate(host,"netsec_ble",NIMBLE_HS_STACK_SIZE,NULL,5,&host_handle)!=pdPASS) {
        (void)ns_ble_stop();return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}
esp_err_t ns_ble_stop(void) {
    if(!ns.ble_initialized)return ESP_OK;
    ns.ble_ready=false;
    if(host_handle && !ns.ble_stopping) {
        (void)ble_gap_disc_cancel();
        int rc=nimble_port_stop();if(rc)return ESP_FAIL;
        ns.ble_stopping=true;
    }
    if(host_handle && !stop_ack) {
        if(xSemaphoreTake(host_done,pdMS_TO_TICKS(2000))!=pdTRUE)return ESP_ERR_TIMEOUT;
        stop_ack=true;
    }
    if(host_handle) { vTaskDelete(host_handle);host_handle=NULL; }
    esp_err_t e=nimble_port_deinit();if(e!=ESP_OK)return e;
    ns.ble_initialized=false;ns.ble_stopping=false;
    vSemaphoreDelete(host_done);host_done=NULL;
    return ESP_OK;
}
void ns_ble_event(const ns_ble_t *b) {
    size_t slot=ns.ble_count;
    for(size_t i=0;i<ns.ble_count;i++)
        if(ns.ble[i].addr_type==b->addr_type && !memcmp(ns.ble[i].mac,b->mac,6)) { slot=i;break; }
    if(slot==NS_BLE_MAX) {
        uint32_t oldest=UINT32_MAX;
        for(size_t i=0;i<NS_BLE_MAX;i++)if(ns.ble[i].last<oldest) { oldest=ns.ble[i].last;slot=i; }
    }
    if(slot==ns.ble_count)ns.ble_count++;
    ns.ble[slot]=*b;
}
