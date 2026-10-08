#include "hub_ai.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <inttypes.h>
#include "esp_crt_bundle.h"
#include "esp_event.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_wifi.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "cJSON.h"

static const char *TAG="hub_ai";
static bool s_wifi_initialized;
static volatile bool s_wifi_online;
static bool s_ntp_initialized;

static bool read_key(nvs_handle_t h,const char *key,char *dst,size_t capacity) {
    size_t n=capacity;
    return nvs_get_str(h,key,dst,&n)==ESP_OK && dst[0]!=0;
}
bool hub_ai_load_connection(hub_ai_connection_t *out) {
    if(!out) return false;
    memset(out,0,sizeof(*out));
    nvs_handle_t h;
    if(nvs_open("hub_ai",NVS_READONLY,&h)!=ESP_OK) return false;
    bool ok=read_key(h,"ssid",out->ssid,sizeof(out->ssid)) &&
        read_key(h,"pass",out->password,sizeof(out->password)) &&
        read_key(h,"gateway",out->gateway,sizeof(out->gateway)) &&
        read_key(h,"token",out->token,sizeof(out->token));
    nvs_close(h);
    if(!ok || strncmp(out->gateway,"https://",8)!=0 ||
       strchr(out->gateway,'?') || strchr(out->gateway,'#') ||
       strlen(out->gateway)>175) return false;
    out->configured=true;
    return true;
}
bool hub_ai_save_connection(const hub_ai_connection_t *c) {
    if(!c || !c->ssid[0] || !c->password[0] || !c->token[0] ||
       strncmp(c->gateway,"https://",8)!=0 ||
       strchr(c->gateway,'?') || strchr(c->gateway,'#') ||
       strlen(c->ssid)>32 || strlen(c->password)>64) return false;
    nvs_handle_t h;
    if(nvs_open("hub_ai",NVS_READWRITE,&h)!=ESP_OK) return false;
    esp_err_t err=nvs_set_str(h,"ssid",c->ssid);
    if(err==ESP_OK) err=nvs_set_str(h,"pass",c->password);
    if(err==ESP_OK) err=nvs_set_str(h,"gateway",c->gateway);
    if(err==ESP_OK) err=nvs_set_str(h,"token",c->token);
    if(err==ESP_OK) err=nvs_commit(h);
    nvs_close(h);
    return err==ESP_OK;
}
uint32_t hub_ai_read_cursor(void) {
    nvs_handle_t h;
    uint32_t cursor=0;
    if(nvs_open("hub_ai",NVS_READONLY,&h)==ESP_OK) {
        (void)nvs_get_u32(h,"cursor",&cursor);
        nvs_close(h);
    }
    return cursor;
}
bool hub_ai_advance_cursor(uint32_t value) {
    if(value<=hub_ai_read_cursor()) return true;
    nvs_handle_t h;
    if(nvs_open("hub_ai",NVS_READWRITE,&h)!=ESP_OK) return false;
    esp_err_t err=nvs_set_u32(h,"cursor",value);
    if(err==ESP_OK) err=nvs_commit(h);
    nvs_close(h);
    return err==ESP_OK;
}
static void wifi_events(void *arg,esp_event_base_t base,int32_t id,void *data) {
    (void)arg;(void)data;
    if(base==WIFI_EVENT && id==WIFI_EVENT_STA_DISCONNECTED) {
        s_wifi_online=false;
        (void)esp_wifi_connect();
    }else if(base==IP_EVENT && id==IP_EVENT_STA_GOT_IP) {
        s_wifi_online=true;
    }
}
bool hub_ai_connect_wifi(const hub_ai_connection_t *c) {
    if(!c || !c->configured) return false;
    if(s_wifi_initialized) return s_wifi_online;
    if(esp_netif_init()!=ESP_OK) return false;
    esp_err_t e=esp_event_loop_create_default();
    if(e!=ESP_OK && e!=ESP_ERR_INVALID_STATE) return false;
    if(!esp_netif_create_default_wifi_sta()) return false;
    wifi_init_config_t init=WIFI_INIT_CONFIG_DEFAULT();
    if(esp_wifi_init(&init)!=ESP_OK) return false;
    (void)esp_event_handler_register(WIFI_EVENT,ESP_EVENT_ANY_ID,wifi_events,NULL);
    (void)esp_event_handler_register(IP_EVENT,IP_EVENT_STA_GOT_IP,wifi_events,NULL);
    wifi_config_t wc={0};
    memcpy(wc.sta.ssid,c->ssid,strlen(c->ssid));
    memcpy(wc.sta.password,c->password,strlen(c->password));
    wc.sta.threshold.authmode=WIFI_AUTH_WPA2_PSK;
    if(esp_wifi_set_mode(WIFI_MODE_STA)!=ESP_OK ||
       esp_wifi_set_config(WIFI_IF_STA,&wc)!=ESP_OK ||
       esp_wifi_start()!=ESP_OK) return false;
    s_wifi_initialized=true;
    (void)esp_wifi_connect();
    return true;
}
typedef struct {
    char content[2048];
    size_t used;
    bool overflow;
} http_result_t;
static esp_err_t http_event(esp_http_client_event_t *evt) {
    if(evt->event_id==HTTP_EVENT_ON_DATA && evt->user_data && evt->data_len>0) {
        http_result_t *r=evt->user_data;
        if(r->used+(size_t)evt->data_len>=sizeof(r->content)) {
            r->overflow=true;
            return ESP_FAIL;
        }
        memcpy(r->content+r->used,evt->data,(size_t)evt->data_len);
        r->used+=(size_t)evt->data_len;
        r->content[r->used]=0;
    }
    return ESP_OK;
}
/* A valid wall clock is essential for server certificate expiry checks. */
static bool ensure_clock(void) {
    time_t now=0;
    time(&now);
    if(now>1700000000) return true;
    if(!s_wifi_online) return false;
    if(!s_ntp_initialized) {
        esp_sntp_config_t cfg=ESP_NETIF_SNTP_DEFAULT_CONFIG("ntp.aliyun.com");
        if(esp_netif_sntp_init(&cfg)!=ESP_OK) return false;
        s_ntp_initialized=true;
    }
    if(esp_netif_sntp_sync_wait(pdMS_TO_TICKS(12000))!=ESP_OK) {
        ESP_LOGW(TAG,"Waiting for trusted wall clock (NTP)");
        return false;
    }
    time(&now);
    return now>1700000000;
}
static bool invoke(const hub_ai_connection_t *c,const char *route,
                   const char *post_data,http_result_t *response) {
    if(!s_wifi_online || !c || !c->configured || !route || !response ||
       !ensure_clock()) return false;
    char url[240];
    if(snprintf(url,sizeof(url),"%s%s",c->gateway,route)>=(int)sizeof(url))
        return false;
    memset(response,0,sizeof(*response));
    esp_http_client_config_t cfg={
        .url=url,
        .timeout_ms=20000,
        .event_handler=http_event,
        .user_data=response,
        .crt_bundle_attach=esp_crt_bundle_attach,
        .disable_auto_redirect=true,
        .buffer_size=1024,
    };
    esp_http_client_handle_t client=esp_http_client_init(&cfg);
    if(!client) return false;
    char auth[140];
    if(snprintf(auth,sizeof(auth),"Bearer %s",c->token)>=(int)sizeof(auth)) {
        esp_http_client_cleanup(client);return false;
    }
    esp_http_client_set_header(client,"Authorization",auth);
    if(post_data) {
        esp_http_client_set_method(client,HTTP_METHOD_POST);
        esp_http_client_set_header(client,"Content-Type","application/json");
        esp_http_client_set_post_field(client,post_data,strlen(post_data));
    }
    esp_err_t err=esp_http_client_perform(client);
    int code=esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);
    if(err!=ESP_OK || code!=200 || response->overflow) {
        ESP_LOGW(TAG,"Gateway failed: transport=%d HTTP=%d",err,code);
        return false;
    }
    return true;
}
bool hub_ai_fetch_settings(const hub_ai_connection_t *c,hub_ai_settings_t *out) {
    if(!out) return false;
    http_result_t result;
    if(!invoke(c,"/api/device/config",NULL,&result)) return false;
    cJSON *json=cJSON_Parse(result.content);
    if(!json) return false;
    const cJSON *en=cJSON_GetObjectItemCaseSensitive(json,"enabled");
    const cJSON *interval=cJSON_GetObjectItemCaseSensitive(json,"interval_minutes");
    const cJSON *count=cJSON_GetObjectItemCaseSensitive(json,"max_records");
    bool ok=cJSON_IsBool(en) && cJSON_IsNumber(interval) &&
        cJSON_IsNumber(count) && interval->valueint>=15 &&
        interval->valueint<=1440 && count->valueint>=1 &&
        count->valueint<=32;
    if(ok) *out=(hub_ai_settings_t){
        .enabled=cJSON_IsTrue(en),
        .interval_minutes=interval->valueint,
        .max_records=count->valueint,
    };
    cJSON_Delete(json);
    return ok;
}
bool hub_ai_summarize(const hub_ai_connection_t *c,
                      const hub_ai_batch_t *batch,hub_ai_digest_t *out) {
    if(!c || !batch || !out || batch->count==0 ||
       batch->count>HUB_AI_MAX_BATCH) return false;
    cJSON *json=cJSON_CreateObject();
    cJSON *items=cJSON_CreateArray();
    if(!json || !items) {cJSON_Delete(json);cJSON_Delete(items);return false;}
    cJSON_AddStringToObject(json,"device_id",HUB_AI_DEVICE_NAME);
    cJSON_AddNumberToObject(json,"after_sequence",batch->after_sequence);
    cJSON_AddNumberToObject(json,"through_sequence",batch->through_sequence);
    cJSON_AddItemToObject(json,"notifications",items);
    for(uint8_t i=0;i<batch->count;i++) {
        const typeof(batch->items[0]) *n=&batch->items[i];
        cJSON *item=cJSON_CreateObject();
        if(!item) {cJSON_Delete(json);return false;}
        cJSON_AddNumberToObject(item,"seq",n->sequence);
        cJSON_AddStringToObject(item,"app",n->app);
        cJSON_AddStringToObject(item,"title",n->title);
        cJSON_AddStringToObject(item,"body",n->body);
        cJSON_AddItemToArray(items,item);
    }
    char *payload=cJSON_PrintUnformatted(json);
    cJSON_Delete(json);
    if(!payload || strlen(payload)>23000) {cJSON_free(payload);return false;}
    http_result_t response;
    bool ok=invoke(c,"/api/device/summarize",payload,&response);
    /* Never print or persist outgoing private notification text. */
    cJSON_free(payload);
    if(!ok) return false;
    cJSON *parsed=cJSON_Parse(response.content);
    if(!parsed) return false;
    const cJSON *txt=cJSON_GetObjectItemCaseSensitive(parsed,"summary");
    const cJSON *through=cJSON_GetObjectItemCaseSensitive(parsed,"processed_through");
    const cJSON *included=cJSON_GetObjectItemCaseSensitive(parsed,"included");
    bool valid=cJSON_IsString(txt) && cJSON_IsNumber(through) &&
        cJSON_IsNumber(included) && through->valuedouble==batch->through_sequence;
    if(valid) {
        size_t n=strlen(txt->valuestring);
        if(n>=HUB_AI_SUMMARY_BYTES) {
            valid=false;
        }else{
            memset(out,0,sizeof(*out));
            memcpy(out->summary,txt->valuestring,n+1);
            out->processed_through=batch->through_sequence;
            out->included=(uint16_t)included->valueint;
        }
    }
    cJSON_Delete(parsed);
    return valid;
}
