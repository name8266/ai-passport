/*
 * Passport embedded AI engine. BLE notifications and archives stay local.
 * Optional user-enabled HTTPS text-only Chat Completions go directly to
 * DeepSeek or another compatible provider. No external gateway is involved.
 */
#include "hub_ai.h"
#include "hub_ai_filter.h"
#include "hub_sound.h"
#include "hub_response.h"
#include "hub_app_catalog.h"
#include "hub_ai_prompt.h"
#include "esp_heap_caps.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "cJSON.h"
#include "esp_crt_bundle.h"
#include "esp_event.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_wifi.h"

static const char *TAG="hub_ai";
static bool s_wifi_init;
static bool s_station_configured;
static volatile bool s_online;
static bool s_sntp_init;
static char s_ap_ssid[33];
static bool init_access(void) {
    uint8_t mac[6]={0};
    if(esp_read_mac(mac,ESP_MAC_WIFI_SOFTAP)!=ESP_OK) return false;
    snprintf(s_ap_ssid,sizeof(s_ap_ssid),"PassportHub-%02X%02X",mac[4],mac[5]);
    return true;
}
bool hub_ai_get_hotspot_ssid(char *ssid,size_t ssid_cap) {
    if(!s_ap_ssid[0])return false;
    if(ssid && ssid_cap)snprintf(ssid,ssid_cap,"%s",s_ap_ssid);
    return true;
}
static void wifi_callback(void *arg,esp_event_base_t base,int32_t id,void *data) {
    (void)arg;
    if(base==WIFI_EVENT && id==WIFI_EVENT_AP_STACONNECTED && data) {
        const wifi_event_ap_staconnected_t *event=data;
        ESP_LOGI(TAG,"Setup hotspot client joined (AID=%d)",event->aid);
        return;
    }
    if(base==WIFI_EVENT && id==WIFI_EVENT_AP_STADISCONNECTED && data) {
        const wifi_event_ap_stadisconnected_t *event=data;
        ESP_LOGI(TAG,"Setup hotspot client left (AID=%d)",event->aid);
        return;
    }
    if(base==WIFI_EVENT &&
       (id==WIFI_EVENT_STA_START || id==WIFI_EVENT_STA_DISCONNECTED)) {
        s_online=false;
        if(s_station_configured) (void)esp_wifi_connect();
    }else if(base==IP_EVENT && id==IP_EVENT_STA_GOT_IP) s_online=true;
}
bool hub_ai_is_online(void) {return s_online;}
uint32_t hub_ai_current_epoch(void) {
    time_t now=0;time(&now);
    return now>=1700000000?(uint32_t)now:0;
}
bool hub_ai_connect_wifi(const hub_ai_connection_t *conn) {
    if(s_wifi_init) return true;
    if(!init_access()) {ESP_LOGE(TAG,"Cannot create admin credentials");return false;}
    if(esp_netif_init()!=ESP_OK) return false;
    esp_err_t ev=esp_event_loop_create_default();
    if(ev!=ESP_OK && ev!=ESP_ERR_INVALID_STATE) return false;
    if(!esp_netif_create_default_wifi_ap()) return false;
    s_station_configured=conn && conn->configured;
    if(s_station_configured && !esp_netif_create_default_wifi_sta()) return false;
    wifi_init_config_t cfg=WIFI_INIT_CONFIG_DEFAULT();
    if(esp_wifi_init(&cfg)!=ESP_OK) return false;
    /* Credentials are owned by hub_ai NVS or a temporary RAM test. Avoid
     * a second implicit credential copy in the Wi-Fi driver NVS namespace. */
    if(esp_wifi_set_storage(WIFI_STORAGE_RAM)!=ESP_OK) return false;
    (void)esp_event_handler_register(WIFI_EVENT,ESP_EVENT_ANY_ID,wifi_callback,NULL);
    (void)esp_event_handler_register(IP_EVENT,IP_EVENT_STA_GOT_IP,wifi_callback,NULL);
    wifi_config_t ap={0};
    memcpy(ap.ap.ssid,s_ap_ssid,strlen(s_ap_ssid));
    ap.ap.ssid_len=strlen(s_ap_ssid);
    ap.ap.max_connection=1;
    ap.ap.authmode=WIFI_AUTH_OPEN;
    if(esp_wifi_set_mode(s_station_configured?WIFI_MODE_APSTA:WIFI_MODE_AP)!=ESP_OK ||
       esp_wifi_set_config(WIFI_IF_AP,&ap)!=ESP_OK) return false;
    if(s_station_configured) {
        wifi_config_t sta={0};
        memcpy(sta.sta.ssid,conn->ssid,strlen(conn->ssid));
        memcpy(sta.sta.password,conn->password,strlen(conn->password));
        sta.sta.threshold.authmode=WIFI_AUTH_WPA2_PSK;
        if(esp_wifi_set_config(WIFI_IF_STA,&sta)!=ESP_OK) return false;
    }
    if(esp_wifi_start()!=ESP_OK) return false;
    s_wifi_init=true;
    /* Start SNTP asynchronously even when AI is disabled so archived notices
     * can be dated and safely expired without blocking the archive worker. */
    if(s_station_configured && !s_sntp_init) {
        esp_sntp_config_t time_config=ESP_NETIF_SNTP_DEFAULT_CONFIG("ntp.aliyun.com");
        if(esp_netif_sntp_init(&time_config)==ESP_OK) s_sntp_init=true;
        else ESP_LOGW(TAG,"Clock sync unavailable; retention waits for valid time");
    }
    hub_ai_web_start();
    ESP_LOGI(TAG,"Open Passport Wi-Fi hotspot enabled (credentials on screen)");
    return true;
}
static bool ensure_clock(void) {
    time_t now=0;time(&now);
    if(now>1700000000) return true;
    if(!s_online) return false;
    if(!s_sntp_init) {
        esp_sntp_config_t config=ESP_NETIF_SNTP_DEFAULT_CONFIG("ntp.aliyun.com");
        if(esp_netif_sntp_init(&config)!=ESP_OK) return false;
        s_sntp_init=true;
    }
    if(esp_netif_sntp_sync_wait(pdMS_TO_TICKS(11000))!=ESP_OK) return false;
    time(&now);
    return now>1700000000;
}
bool hub_ai_fetch_settings(const hub_ai_connection_t *c,hub_ai_settings_t *out) {
    (void)c;
    /* The web backend resides entirely on Passport's NVS / HTTP server. */
    if(!s_wifi_init || !out) return false;
    hub_ai_settings_t local;
    if(!hub_ai_read_settings(&local)) return false;
    *out=local;
    return s_online;
}
static esp_err_t receive_response(esp_http_client_event_t *ev) {
    if(ev->event_id!=HTTP_EVENT_ON_DATA || !ev->user_data || ev->data_len<=0)
        return ESP_OK;
    return hub_response_append(ev->user_data,ev->data,(size_t)ev->data_len)?
        ESP_OK:ESP_FAIL;
}
static bool excluded(const char *apps,const char *app) {
    if(!apps[0])return false;
    char copy[192];
    snprintf(copy,sizeof(copy),"%s",apps);
    char *save=NULL;
    for(char *it=strtok_r(copy,",",&save);it;it=strtok_r(NULL,",",&save)) {
        while(*it==' ') it++;
        char *end=it+strlen(it);
        while(end>it && end[-1]==' ') *--end=0;
        if(strcmp(it,app)==0) return true;
    }
    return false;
}
static bool abort_json_request(cJSON *request,cJSON *unattached) {
    cJSON_Delete(unattached);cJSON_Delete(request);
    hub_ai_web_start();hub_sound_network_end();return false;
}
bool hub_ai_summarize_context(const hub_ai_connection_t *conn,
                      const hub_ai_settings_t *configuration,
                      const hub_ai_batch_t *batch,const hub_ai_digest_t *prior,
                      hub_ai_digest_t *out) {
    (void)conn;
    if(!out||!batch||batch->count==0||batch->count>HUB_AI_MAX_BATCH ||
       !s_online || !ensure_clock()) return false;
    /* The caller owns the credentials; diagnostic calls can keep them in RAM. */
    if(!configuration || !configuration->enabled || !configuration->api_key[0])
        return false;
    const hub_ai_settings_t *settings=configuration;
    /* The rolling digest merges small batches without re-sending or keeping
     * all source notifications in RAM. Only same-day context is accepted. */
    bool use_prior=prior && batch->day_tag &&
        prior->day_tag==batch->day_tag &&
        prior->processed_through<=batch->after_sequence && prior->summary[0];
    static char payload[3584];
    size_t at=0;
    int count=0;
    int head=snprintf(payload,sizeof(payload),
        "【此前同日摘要】\n%s\n【本批新通知预览】\n",
        use_prior?prior->summary:"（无；从本批开始）");
    if(head<0 || (size_t)head>=sizeof(payload))return false;
    at=(size_t)head;
    for(int i=0;i<batch->count;i++) {
        const char *app=batch->items[i].app;
        const char *title=batch->items[i].title;
        const char *body=batch->items[i].body;
        if(excluded(settings->excluded_apps,app) ||
           (settings->redact_sensitive && (batch->items[i].sensitive || hub_ai_sensitive(title,body)))) continue;
        int w=snprintf(payload+at,sizeof(payload)-at,
            "\n[类别:%s][应用:%s] %s - %s",
            hub_catalog_category(app),hub_catalog_display(app),title,body);
        if(w<0 || (size_t)w>=sizeof(payload)-at) break;
        at+=(size_t)w;
        count++;
    }
    if(count==0) {
        snprintf(out->summary,sizeof(out->summary),"%s",
            use_prior?prior->summary:"本时段无可分析的通知；可能被隐私规则过滤。");
        out->included=use_prior?prior->included:0;
        out->processed_through=batch->through_sequence;
        out->day_tag=batch->day_tag;
        return true;
    }
    if(!hub_sound_network_begin()) return false;
    if(!hub_ai_web_pause()) {hub_sound_network_end();return false;}
    /* TLS handshakes need contiguous internal RAM; defer under pressure
     * rather than risk taking down BLE capture and the local journal. */
    size_t free_heap=heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT);
    size_t largest=heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT);
    if(free_heap<26000u || largest<11000u) {
        ESP_LOGW(TAG,"Deferring TLS: heap=%u largest=%u",
                 (unsigned)free_heap,(unsigned)largest);
        hub_ai_web_start();hub_sound_network_end();return false;
    }
    cJSON *request=cJSON_CreateObject();
    cJSON *messages=cJSON_CreateArray();
    if(!request || !messages || !cJSON_AddItemToObject(request,"messages",messages))
        return abort_json_request(request,messages);
    if(!cJSON_AddStringToObject(request,"model",settings->model) ||
       !cJSON_AddBoolToObject(request,"stream",false) ||
       !cJSON_AddNumberToObject(request,"max_tokens",450))
        return abort_json_request(request,NULL);
    if(strstr(settings->endpoint,"api.deepseek.com/")) {
        /* Flash summaries need short user-facing output, not a costly chain of thought. */
        cJSON *thinking=cJSON_CreateObject();
        if(!thinking || !cJSON_AddStringToObject(thinking,"type","disabled") ||
           !cJSON_AddItemToObject(request,"thinking",thinking))
            return abort_json_request(request,thinking);
    }
    cJSON *system=cJSON_CreateObject();
    if(!system || !cJSON_AddStringToObject(system,"role","system") ||
       !cJSON_AddStringToObject(system,"content",HUB_AI_SYSTEM_PROMPT) ||
       !cJSON_AddItemToArray(messages,system))
        return abort_json_request(request,system);
    cJSON *user=cJSON_CreateObject();
    if(!user || !cJSON_AddStringToObject(user,"role","user") ||
       !cJSON_AddStringToObject(user,"content",payload) ||
       !cJSON_AddItemToArray(messages,user))
        return abort_json_request(request,user);
    char *json=cJSON_PrintUnformatted(request);
    cJSON_Delete(request);
    if(!json) {hub_ai_web_start();hub_sound_network_end();return false;}
    bool ok=false, http_ok=false;
    hub_response_t response={.cap=5120};
    esp_http_client_config_t cfg={
        .url=settings->endpoint,
        .timeout_ms=30000,
        /* The device connects over IPv4 STA; do not block on AAAA-only DNS. */
        .addr_type=HTTP_ADDR_TYPE_INET,
        .crt_bundle_attach=esp_crt_bundle_attach,
        .disable_auto_redirect=true,
        .buffer_size=512,
        .buffer_size_tx=512,
        .event_handler=receive_response,
        .user_data=&response,
    };
    static char auth[208];
    esp_http_client_handle_t client=esp_http_client_init(&cfg);
    if(client) {
        int length=snprintf(auth,sizeof(auth),"Bearer %s",settings->api_key);
        if(length>0 && length<(int)sizeof(auth)) {
            esp_http_client_set_header(client,"Authorization",auth);
            esp_http_client_set_header(client,"Content-Type","application/json");
            esp_http_client_set_method(client,HTTP_METHOD_POST);
            esp_http_client_set_post_field(client,json,strlen(json));
            esp_err_t e=esp_http_client_perform(client);
            int status=esp_http_client_get_status_code(client);
            ESP_LOGI(TAG,"Direct model HTTPS status=%d err=%d response_bytes=%u",
                status,e,(unsigned)response.used);
            http_ok=(e==ESP_OK && status==200 && !response.overflow && response.data);
            if(!http_ok) ESP_LOGW(TAG,"Direct model HTTPS status=%d err=%d",status,e);
        }
        /* CRITICAL: free the TLS session and its handshake/socket buffers
         * BEFORE allocating cJSON's response DOM. The former implementation
         * kept both alive, causing severe transient heap exhaustion on C3. */
        esp_http_client_cleanup(client);
    }
    memset(auth,0,sizeof(auth));
    memset(json,0,strlen(json));cJSON_free(json);
    if(http_ok) {
        cJSON *answer=cJSON_Parse(response.data);
        cJSON *choices=cJSON_GetObjectItemCaseSensitive(answer,"choices");
        cJSON *one=cJSON_GetArrayItem(choices,0);
        cJSON *message=cJSON_GetObjectItemCaseSensitive(one,"message");
        cJSON *content=cJSON_GetObjectItemCaseSensitive(message,"content");
        if(cJSON_IsString(content) && content->valuestring && content->valuestring[0]) {
            memset(out,0,sizeof(*out));
            hub_utf8_copy(out->summary,sizeof(out->summary),
                (const uint8_t *)content->valuestring,strlen(content->valuestring));
            out->processed_through=batch->through_sequence;
            out->day_tag=batch->day_tag;
            unsigned cumulative=(unsigned)(use_prior?prior->included:0)+(unsigned)count;
            out->included=(uint16_t)(cumulative>65535u?65535u:cumulative);
            ok=out->summary[0]!=0;
        }
        cJSON_Delete(answer);
    }
    hub_response_release(&response);
    memset(auth,0,sizeof(auth));
    hub_ai_web_start();
    hub_sound_network_end();
    return ok;
}

/* Backward-compatible entry point for direct device diagnostics. */
bool hub_ai_summarize_settings(const hub_ai_connection_t *conn,
                 const hub_ai_settings_t *configuration,
                 const hub_ai_batch_t *batch,hub_ai_digest_t *out) {
    return hub_ai_summarize_context(conn,configuration,batch,NULL,out);
}

/* Production path uses NVS settings; temporary tests use the same HTTPS engine. */
bool hub_ai_summarize(const hub_ai_connection_t *conn,
                      const hub_ai_batch_t *batch,hub_ai_digest_t *out) {
    static hub_ai_settings_t settings;
    bool ok=hub_ai_read_settings(&settings) &&
        hub_ai_summarize_settings(conn,&settings,batch,out);
    memset(settings.api_key,0,sizeof(settings.api_key));
    return ok;
}
