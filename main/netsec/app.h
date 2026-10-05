#pragma once
#include "core.h"
#ifdef NETSEC_HOST_TEST
#include "netsec_platform.h"
#else
#include "bsp_button.h"
#include "esp_err.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#endif

typedef enum { NS_HOME, NS_WIFI, NS_CHANNELS, NS_SIGNAL, NS_MONITOR,
    NS_DETECT, NS_BLE, NS_LAN, NS_SERVICES, NS_CAPTURE, NS_EAPOL,
    NS_HISTORY, NS_SETTINGS } ns_page_t;

typedef struct {
    uint32_t version;
    uint8_t channel, hop, brightness, language, region, history_enabled;
    uint16_t threshold;
} ns_settings_t;
typedef struct { char ssid[97]; uint8_t bssid[6], channel, auth; int8_t rssi; } ns_ap_t;
typedef struct {
    uint8_t mac[6], addr_type, data[31], len, event_type;
    int8_t rssi; char name[65]; uint32_t last;
} ns_ble_t;
typedef struct {
    uint64_t us; uint16_t original; uint8_t saved, channel;
    int8_t rssi; uint8_t data[NS_SNAPLEN];
} ns_capture_t;
typedef struct {
    uint32_t frames[3], beacon, probe_req, probe_resp, deauth, disassoc;
    uint32_t malformed, captured, overwritten, alerts, pps, eapol[4];
    uint16_t reason;
    uint8_t source[6]; int8_t signal;
    ns_burst_t burst;
    ns_session_t sessions[NS_SESSION_MAX];
} ns_stats_t;
typedef struct { uint32_t boot, seconds; uint16_t aps; uint8_t best;
    int8_t strongest; uint16_t count[14]; } ns_history_t;
typedef struct { char kind[8], name[97], detail[129]; uint32_t ip, ttl; uint16_t port; } ns_service_t;
typedef enum { NSE_KEY, NSE_SCAN, NSE_IP, NSE_DISCONNECT, NSE_BLE, NSE_BLE_READY, NSE_BLE_RESET } ns_event_type_t;
typedef struct {
    ns_event_type_t type;
    uint32_t generation;
    union { struct {bsp_btn_t btn;bsp_btn_ev_t ev;} key;
        ns_ble_t ble; int code; };
} ns_event_t;
typedef struct {
    ns_page_t page;
    int selected, home_selected, detail_page;
    bool detail, paused, wifi_ready, scanning, connected, capture_on;
    bool ble_ready, ble_initialized, ble_stopping, lan_connecting;
    uint8_t channel_max;
    uint32_t scan_total, scan_time, ble_dropped, free_heap, min_heap;
    uint32_t boot_id, radio_generation;
    ns_settings_t settings;
    ns_ap_t aps[NS_AP_MAX], target;
    size_t ap_count;
    bool target_set;
    ns_ble_t ble[NS_BLE_MAX]; size_t ble_count;
    ns_service_t services[NS_SERVICE_MAX];size_t service_count;
    ns_history_t history[NS_HISTORY_MAX];size_t history_count;
    ns_stats_t stats;
    int8_t signal[60]; size_t signal_count;
    uint16_t channels[14];uint32_t scores[14];
    esp_netif_t *netif;
    QueueHandle_t events;
    char status[97], lan_text[256];
} ns_app_t;
extern ns_app_t ns;

esp_err_t ns_wifi_start(void);
esp_err_t ns_radio_stop(void);
esp_err_t ns_scan_start(void);
void ns_scan_done(void);
esp_err_t ns_sniff_start(void);
esp_err_t ns_set_channel(uint8_t channel);
void ns_radio_tick(uint64_t now);
void ns_stats_read(ns_stats_t *out);
void ns_stats_clear(void);
void ns_capture_enable(bool on);
void ns_capture_clear(void);
void ns_capture_export(void);
esp_err_t ns_ble_start(void);
esp_err_t ns_ble_stop(void);
esp_err_t ns_ble_resume(void);
void ns_ble_event(const ns_ble_t *e);
esp_err_t ns_lan_connect(void);
void ns_lan_info(void);
void ns_discovery_start(void);
void ns_discovery_stop(void);
void ns_discovery_tick(uint64_t now);
void ns_ui_init(void);
void ns_ui_render(void);
void ns_persist_settings(void);
void ns_persist_history(void);
void ns_command(char *line);
void ns_go(ns_page_t page);
const char *ns_auth(uint8_t mode);
void ns_mac(char out[18], const uint8_t mac[6]);
