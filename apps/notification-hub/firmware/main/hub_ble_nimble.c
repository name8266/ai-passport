/*
 * Passport's compact Apple ANCS consumer for ESP-IDF 5.5.3 NimBLE.
 * Receives iOS-issued previews only; application persistence remains in
 * app_main and the FAT archive worker. No on-device task is created here
 * beyond the single NimBLE host task managed by nimble_port_freertos.
 */
#include "hub_ble_nimble.h"
#include <string.h>
#include "esp_log.h"
#include "esp_random.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"
#include "os/os_mbuf.h"

static const char *TAG="hub_ble";
static hub_ble_callbacks_t s_callbacks;
static uint16_t s_conn=BLE_HS_CONN_HANDLE_NONE;
static uint16_t s_service_start,s_service_end;
static uint16_t s_source,s_data,s_control,s_source_def,s_data_def,s_control_def;
static uint16_t s_data_cccd,s_source_cccd;
static uint8_t s_address_type;
static uint8_t s_stage;
static bool s_connected,s_paired,s_ready,s_discovery_started;
static uint32_t s_passkey;

static const ble_uuid128_t k_ancs=BLE_UUID128_INIT(
    0xD0,0x00,0x2D,0x12,0x1E,0x4B,0x0F,0xA4,0x99,0x4E,0xCE,0xB5,0x31,0xF4,0x05,0x79);
static const ble_uuid128_t k_source=BLE_UUID128_INIT(
    0xBD,0x1D,0xA2,0x99,0xE6,0x25,0x58,0x8C,0xD9,0x42,0x01,0x63,0x0D,0x12,0xBF,0x9F);
static const ble_uuid128_t k_data=BLE_UUID128_INIT(
    0xFB,0x7B,0x7C,0xCE,0x6A,0xB3,0x44,0xBE,0xB5,0x4B,0xD6,0x24,0xE9,0xC6,0xEA,0x22);
static const ble_uuid128_t k_control=BLE_UUID128_INIT(
    0xD9,0xD9,0xAA,0xFD,0xBD,0x9B,0x21,0x98,0xA8,0x49,0xE1,0x45,0xF3,0xD8,0xD1,0x69);
/* Solicited Service UUID (AD type 0x15), not an ANCS GATT server claim. */
static const uint8_t k_adv[]={
    2,0x01,0x06,17,0x15,0xD0,0x00,0x2D,0x12,0x1E,0x4B,0x0F,0xA4,
    0x99,0x4E,0xCE,0xB5,0x31,0xF4,0x05,0x79
};
static const uint8_t k_scan[]={
    11,0x09,'N','o','t','i','f','y',' ','H','u','b'
};

static void signal_link(void) {
    if(s_callbacks.link)
        s_callbacks.link(s_connected,s_paired,s_ready,s_passkey);
}
static void reset_link(void) {
    s_conn=BLE_HS_CONN_HANDLE_NONE;
    s_connected=s_paired=s_ready=s_discovery_started=false;
    s_service_start=s_service_end=s_source=s_data=s_control=0;
    s_source_def=s_data_def=s_control_def=0;
    s_data_cccd=s_source_cccd=0;
    s_stage=0;
    signal_link();
}
static int gap_event(struct ble_gap_event *event,void *arg);
static void advertise(void) {
    if(!ble_hs_synced() || s_connected || ble_gap_adv_active())return;
    struct ble_gap_adv_params params={0};
    params.conn_mode=BLE_GAP_CONN_MODE_UND;
    params.disc_mode=BLE_GAP_DISC_MODE_GEN;
    int rc=ble_gap_adv_set_data(k_adv,sizeof(k_adv));
    if(rc==0)rc=ble_gap_adv_rsp_set_data(k_scan,sizeof(k_scan));
    if(rc==0)rc=ble_gap_adv_start(s_address_type,NULL,BLE_HS_FOREVER,
                                  &params,gap_event,NULL);
    if(rc!=0)ESP_LOGW(TAG,"ANCS advertising rc=%d",rc);
}

static int desc_write_cb(uint16_t conn,const struct ble_gatt_error *error,
                         struct ble_gatt_attr *attr,void *arg);
static uint16_t descriptor_end(uint16_t value) {
    uint16_t end=s_service_end;
    const uint16_t defs[]={s_source_def,s_data_def,s_control_def};
    for(size_t i=0;i<sizeof(defs)/sizeof(defs[0]);i++)
        if(defs[i]>value && (uint16_t)(defs[i]-1u)<end)
            end=(uint16_t)(defs[i]-1u);
    return end;
}
static int desc_cb(uint16_t conn,const struct ble_gatt_error *error,
                   uint16_t chr_val_handle,const struct ble_gatt_dsc *dsc,void *arg) {
    (void)chr_val_handle;(void)arg;
    if(error->status==0 && dsc &&
       ble_uuid_cmp(&dsc->uuid.u,BLE_UUID16_DECLARE(BLE_GATT_DSC_CLT_CFG_UUID16))==0) {
        if(s_stage==1)s_data_cccd=dsc->handle;
        if(s_stage==2)s_source_cccd=dsc->handle;
        return 0;
    }
    if(error->status!=BLE_HS_EDONE)return 0;
    uint16_t cccd=s_stage==1?s_data_cccd:s_source_cccd;
    if(!cccd) {
        ESP_LOGW(TAG,"ANCS CCCD not found; stage=%u",(unsigned)s_stage);
        return 0;
    }
    static const uint8_t enable[]={1,0};
    int rc=ble_gattc_write_flat(conn,cccd,enable,sizeof(enable),
                                 desc_write_cb,NULL);
    if(rc!=0)ESP_LOGW(TAG,"ANCS CCCD write rc=%d",rc);
    return 0;
}
static int discover_cccd(uint16_t val_handle) {
    uint16_t end=descriptor_end(val_handle);
    if(!val_handle || end<=val_handle)return -1;
    return ble_gattc_disc_all_dscs(s_conn,val_handle,end,desc_cb,NULL);
}
static int desc_write_cb(uint16_t conn,const struct ble_gatt_error *error,
                         struct ble_gatt_attr *attr,void *arg) {
    (void)attr;(void)arg;
    if(error->status!=0) {
        ESP_LOGW(TAG,"ANCS CCCD rejected status=%d",error->status);
        return 0;
    }
    if(s_stage==1) {
        s_stage=2;
        int rc=discover_cccd(s_source);
        if(rc!=0)ESP_LOGW(TAG,"Source CCCD discovery rc=%d",rc);
    }else if(s_stage==2) {
        s_stage=3;
        s_ready=true;
        signal_link();
        ESP_LOGI(TAG,"ANCS subscriptions ready");
    }
    (void)conn;
    return 0;
}
static int char_cb(uint16_t conn,const struct ble_gatt_error *error,
                   const struct ble_gatt_chr *chr,void *arg) {
    (void)arg;
    if(error->status==0 && chr) {
        if(ble_uuid_cmp(&chr->uuid.u,&k_source.u)==0 &&
           (chr->properties & BLE_GATT_CHR_PROP_NOTIFY)) {
            s_source=chr->val_handle;s_source_def=chr->def_handle;
        }else if(ble_uuid_cmp(&chr->uuid.u,&k_data.u)==0 &&
                 (chr->properties & BLE_GATT_CHR_PROP_NOTIFY)) {
            s_data=chr->val_handle;s_data_def=chr->def_handle;
        }else if(ble_uuid_cmp(&chr->uuid.u,&k_control.u)==0 &&
                 (chr->properties & BLE_GATT_CHR_PROP_WRITE)) {
            s_control=chr->val_handle;s_control_def=chr->def_handle;
        }
    }else if(error->status==BLE_HS_EDONE) {
        if(s_source && s_data && s_control) {
            s_stage=1;
            int rc=discover_cccd(s_data);
            if(rc!=0)ESP_LOGW(TAG,"Data CCCD discovery rc=%d",rc);
        }else {
            ESP_LOGW(TAG,"ANCS missing required GATT attributes");
        }
    }
    (void)conn;
    return 0;
}
static int svc_cb(uint16_t conn,const struct ble_gatt_error *error,
                  const struct ble_gatt_svc *svc,void *arg) {
    (void)arg;
    if(error->status==0 && svc) {
        s_service_start=svc->start_handle;
        s_service_end=svc->end_handle;
    }else if(error->status==BLE_HS_EDONE && s_service_start && s_service_end) {
        int rc=ble_gattc_disc_all_chrs(conn,s_service_start,s_service_end,
                                       char_cb,NULL);
        if(rc!=0)ESP_LOGW(TAG,"ANCS characteristics discovery rc=%d",rc);
    }
    return 0;
}
static void begin_discovery(void) {
    if(!s_connected || !s_paired || s_discovery_started)return;
    s_discovery_started=true;
    int rc=ble_gattc_disc_svc_by_uuid(s_conn,&k_ancs.u,svc_cb,NULL);
    if(rc!=0) {
        s_discovery_started=false;
        ESP_LOGW(TAG,"ANCS discovery rc=%d",rc);
    }
}
static int write_response(uint16_t conn,const struct ble_gatt_error *error,
                          struct ble_gatt_attr *attr,void *arg) {
    (void)conn;(void)attr;(void)arg;
    if(error->status!=0 && s_callbacks.write_error)s_callbacks.write_error();
    return 0;
}
bool hub_ble_nimble_request_details(const uint8_t *bytes,size_t len) {
    if(!s_ready || !s_paired || !s_control || !bytes || !len)return false;
    return ble_gattc_write_flat(s_conn,s_control,bytes,(uint16_t)len,
                                 write_response,NULL)==0;
}
void hub_ble_nimble_disconnect(void) {
    if(s_conn!=BLE_HS_CONN_HANDLE_NONE)
        (void)ble_gap_terminate(s_conn,BLE_ERR_REM_USER_CONN_TERM);
}
static int gap_event(struct ble_gap_event *event,void *arg) {
    (void)arg;
    switch(event->type) {
    case BLE_GAP_EVENT_CONNECT:
        if(event->connect.status!=0){advertise();return 0;}
        reset_link();
        s_conn=event->connect.conn_handle;
        s_connected=true;
        signal_link();
        if(ble_gap_security_initiate(s_conn)!=0) {
            ESP_LOGW(TAG,"Could not start bonding; disconnecting");
            hub_ble_nimble_disconnect();
        }
        return 0;
    case BLE_GAP_EVENT_DISCONNECT:
        reset_link();
        advertise();
        return 0;
    case BLE_GAP_EVENT_ADV_COMPLETE:
        advertise();
        return 0;
    case BLE_GAP_EVENT_ENC_CHANGE: {
        struct ble_gap_conn_desc desc;
        if(event->enc_change.status==0 &&
           ble_gap_conn_find(event->enc_change.conn_handle,&desc)==0) {
            s_paired=desc.sec_state.encrypted && desc.sec_state.authenticated;
            signal_link();
            if(s_paired)begin_discovery();
        }
        return 0;
    }
    case BLE_GAP_EVENT_PASSKEY_ACTION: {
        struct ble_sm_io io={0};
        io.action=event->passkey.params.action;
        if(io.action==BLE_SM_IOACT_DISP) {
            s_passkey=esp_random()%1000000u;
            io.passkey=s_passkey;
            signal_link();
        }else if(io.action==BLE_SM_IOACT_NUMCMP) {
            io.numcmp_accept=0; /* No hardware confirmation screen available. */
        }else return 0;
        (void)ble_sm_inject_io(event->passkey.conn_handle,&io);
        return 0;
    }
    case BLE_GAP_EVENT_NOTIFY_RX: {
        if(!s_ready || !s_paired)return 0;
        uint16_t length=(uint16_t)OS_MBUF_PKTLEN(event->notify_rx.om);
        if(length==0 || length>247u)return 0;
        uint8_t buffer[247];
        if(os_mbuf_copydata(event->notify_rx.om,0,length,buffer)!=0)return 0;
        if(event->notify_rx.attr_handle==s_source && s_callbacks.source)
            s_callbacks.source(buffer,length);
        else if(event->notify_rx.attr_handle==s_data && s_callbacks.data)
            s_callbacks.data(buffer,length);
        return 0;
    }
    case BLE_GAP_EVENT_REPEAT_PAIRING:
        /* Do not silently discard customer bonds; require explicit reset. */
        return BLE_GAP_REPEAT_PAIRING_IGNORE;
    default:return 0;
    }
}
static void on_reset(int reason) {
    ESP_LOGW(TAG,"NimBLE reset reason=%d",reason);
    reset_link();
}
static void on_sync(void) {
    int rc=ble_hs_util_ensure_addr(0);
    if(rc==0)rc=ble_hs_id_infer_auto(0,&s_address_type);
    if(rc==0)advertise();
    else ESP_LOGE(TAG,"NimBLE address setup rc=%d",rc);
}
static void host_task(void *arg) {
    (void)arg;
    nimble_port_run();
    nimble_port_freertos_deinit();
}
void ble_store_config_init(void);
esp_err_t hub_ble_nimble_start(const hub_ble_callbacks_t *callbacks) {
    if(!callbacks)return ESP_ERR_INVALID_ARG;
    s_callbacks=*callbacks;
    int rc=nimble_port_init();
    if(rc!=ESP_OK)return rc;
    ble_hs_cfg.reset_cb=on_reset;
    ble_hs_cfg.sync_cb=on_sync;
    ble_hs_cfg.store_status_cb=ble_store_util_status_rr;
    ble_hs_cfg.sm_bonding=1;
    ble_hs_cfg.sm_sc=1;
    ble_hs_cfg.sm_mitm=1;
    ble_hs_cfg.sm_io_cap=BLE_HS_IO_DISPLAY_ONLY;
    ble_hs_cfg.sm_our_key_dist=BLE_SM_PAIR_KEY_DIST_ENC|BLE_SM_PAIR_KEY_DIST_ID;
    ble_hs_cfg.sm_their_key_dist=BLE_SM_PAIR_KEY_DIST_ENC|BLE_SM_PAIR_KEY_DIST_ID;
    ble_svc_gap_device_name_set("Notify Hub");
    ble_store_config_init();
    nimble_port_freertos_init(host_task);
    return ESP_OK;
}
