#include "battery_web.h"
#include "battery_store.h"
#include "battery_protocol.h"
#include "cJSON.h"
#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_random.h"
#include "esp_wifi.h"
#include "esp_wifi_default.h"
#include "esp_heap_caps.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern const unsigned char battery_html_start[] asm("_binary_battery_html_gz_start");
extern const unsigned char battery_html_end[] asm("_binary_battery_html_gz_end");
static httpd_handle_t s_server;
static esp_netif_t *s_netif;
static bool s_network_ready, s_wifi_init, s_wifi_started;
static char s_ssid[32], s_password[13];
static const char *TAG="battery_web";

static void headers(httpd_req_t *r) {
    httpd_resp_set_hdr(r,"Cache-Control","no-store");
    httpd_resp_set_hdr(r,"X-Content-Type-Options","nosniff");
    httpd_resp_set_hdr(r,"Content-Security-Policy","default-src 'self'; script-src 'self' 'unsafe-inline'; style-src 'self' 'unsafe-inline'; img-src 'self' data:; connect-src 'self'; frame-ancestors 'none'; base-uri 'none'; form-action 'self'");
}
static esp_err_t json_error(httpd_req_t *r,const char *status,const char *message) {
    headers(r); httpd_resp_set_status(r,status); httpd_resp_set_type(r,"application/json");
    return httpd_resp_send(r,message,HTTPD_RESP_USE_STRLEN);
}
static esp_err_t send_json(httpd_req_t *r,cJSON *doc) {
    if (!doc) return json_error(r,"503 Service Unavailable","{\"error\":\"内存不足，请重试\"}");
    char *text=cJSON_PrintUnformatted(doc); cJSON_Delete(doc);
    if (!text) return json_error(r,"503 Service Unavailable","{\"error\":\"内存不足，请重试\"}");
    headers(r); httpd_resp_set_type(r,"application/json");
    esp_err_t err=httpd_resp_send(r,text,HTTPD_RESP_USE_STRLEN); free(text); return err;
}
static esp_err_t page(httpd_req_t *r) {
    headers(r); httpd_resp_set_type(r,"text/html; charset=utf-8");
    httpd_resp_set_hdr(r,"Content-Encoding","gzip");
    return httpd_resp_send(r,(const char *)battery_html_start,battery_html_end-battery_html_start);
}
static cJSON *asset_json(const bat_asset_t *a) {
    cJSON *o=cJSON_CreateObject(); if (!o) return NULL;
    cJSON_AddNumberToObject(o,"id",a->id); cJSON_AddStringToObject(o,"name",a->name);
    cJSON_AddStringToObject(o,"location",a->location); cJSON_AddStringToObject(o,"notes",a->notes);
    cJSON_AddNumberToObject(o,"capacity",a->capacity_mah); cJSON_AddNumberToObject(o,"cycles",a->cycles);
    cJSON_AddNumberToObject(o,"soc",a->soc); cJSON_AddNumberToObject(o,"health",a->health);
    cJSON_AddNumberToObject(o,"status",a->status); cJSON_AddNumberToObject(o,"chemistry",a->chemistry);
    return o;
}
static esp_err_t state(httpd_req_t *r) {
    bat_db_t *db=malloc(sizeof(*db));
    if (!db || !battery_store_snapshot(db)) { free(db); return json_error(r,"503 Service Unavailable","{\"error\":\"设备忙，请重试\"}"); }
    cJSON *o=cJSON_CreateObject(), *assets=cJSON_CreateArray(), *events=cJSON_CreateArray();
    if (!o || !assets || !events) { cJSON_Delete(o); cJSON_Delete(assets); cJSON_Delete(events); free(db); return send_json(r,NULL); }
    cJSON_AddNumberToObject(o,"revision",db->revision); cJSON_AddNumberToObject(o,"limit",BAT_MAX_ASSETS);
    cJSON_AddBoolToObject(o,"writable",battery_store_writable());
    int tz=0; int64_t epoch=battery_time_now(&tz);
    cJSON_AddNumberToObject(o,"epoch",(double)epoch); cJSON_AddNumberToObject(o,"timezone",tz);
    cJSON_AddItemToObject(o,"assets",assets); cJSON_AddItemToObject(o,"events",events);
    for (unsigned i=0;i<db->count;++i) cJSON_AddItemToArray(assets,asset_json(&db->assets[i]));
    for (unsigned i=0;i<db->event_count;++i) {
        const bat_event_t *e=&db->events[i]; cJSON *event=cJSON_CreateObject();
        if (!event) { cJSON_Delete(o); free(db); return send_json(r,NULL); }
        cJSON_AddNumberToObject(event,"id",e->asset_id); cJSON_AddNumberToObject(event,"sequence",e->sequence);
        cJSON_AddNumberToObject(event,"action",e->action); cJSON_AddNumberToObject(event,"epoch",(double)e->epoch);
        cJSON_AddNumberToObject(event,"from",e->from_status); cJSON_AddNumberToObject(event,"to",e->to_status);
        cJSON_AddItemToArray(events,event);
    }
    free(db); return send_json(r,o);
}
/* Cross-origin requests cannot supply this custom header without a preflight.
 * No CORS/OPTIONS route exists. Also reject a foreign Origin or Host. */
static bool same_origin(httpd_req_t *r) {
    char client[24], host[64], origin[80], expected[80], type[40];
    if (httpd_req_get_hdr_value_str(r,"X-Passport-Client",client,sizeof(client))!=ESP_OK
        || strcmp(client,"battery-desk")!=0
        || httpd_req_get_hdr_value_str(r,"Content-Type",type,sizeof(type))!=ESP_OK
        || strcmp(type,"application/json")!=0
        || httpd_req_get_hdr_value_str(r,"Host",host,sizeof(host))!=ESP_OK
        || (strcmp(host,"192.168.4.1") && strcmp(host,"192.168.4.1:80"))) return false;
    if (!httpd_req_get_hdr_value_len(r,"Origin")) return true;
    if (httpd_req_get_hdr_value_str(r,"Origin",origin,sizeof(origin))!=ESP_OK) return false;
    snprintf(expected,sizeof(expected),"http://%s",host);
    return strcmp(origin,expected)==0;
}
static cJSON *body(httpd_req_t *r) {
    if (!same_origin(r)) { json_error(r,"403 Forbidden","{\"error\":\"请求来源无效\"}"); return NULL; }
    if (!r->content_len || r->content_len>1536) { json_error(r,"413 Payload Too Large","{\"error\":\"请求过大\"}"); return NULL; }
    char *buffer=malloc(r->content_len+1); if (!buffer) { send_json(r,NULL); return NULL; }
    size_t done=0;
    while (done<r->content_len) {
        int got=httpd_req_recv(r,buffer+done,r->content_len-done);
        if (got<=0) { free(buffer); json_error(r,"408 Request Timeout","{\"error\":\"请求中断，请重试\"}"); return NULL; }
        done+=(size_t)got;
    }
    buffer[done]=0;
    /* NULs and trailing input must not truncate validation. */
    const char *end=NULL;
    cJSON *o=!battery_payload_safe(buffer,done) ? NULL : cJSON_ParseWithLengthOpts(buffer,done+1,&end,true);
    if (!cJSON_IsObject(o)) { cJSON_Delete(o); o=NULL; json_error(r,"400 Bad Request","{\"error\":\"无效 JSON\"}"); }
    if (o) {
        for (cJSON *v=o->child;v;v=v->next) {
            for (cJSON *other=v->next;other;other=other->next) {
                if (strcmp(v->string,other->string)==0) {
                    cJSON_Delete(o); o=NULL;
                    json_error(r,"400 Bad Request","{\"error\":\"重复字段\"}");
                    goto parsed;
                }
            }
        }
    }
parsed:
    free(buffer); return o;
}
static bool number(cJSON *o,const char *key,double min,double max,double *out) {
    cJSON *v=cJSON_GetObjectItemCaseSensitive(o,key);
    if (!cJSON_IsNumber(v) || !isfinite(v->valuedouble) || v->valuedouble<min
        || v->valuedouble>max || floor(v->valuedouble)!=v->valuedouble) return false;
    *out=v->valuedouble; return true;
}
static bool text_field(cJSON *o,const char *key,char *out,size_t capacity) {
    cJSON *v=cJSON_GetObjectItemCaseSensitive(o,key);
    if (!cJSON_IsString(v) || strlen(v->valuestring)>=capacity) return false;
    strcpy(out,v->valuestring); return true;
}
static esp_err_t mutation_result(httpd_req_t *r,esp_err_t err,bat_result_t result) {
    if (err!=ESP_OK) return json_error(r,"503 Service Unavailable","{\"error\":\"保存失败，数据未更新。请检查存储并重试\"}");
    switch (result) {
        case BAT_OK: return json_error(r,"200 OK","{\"ok\":true}");
        case BAT_CONFLICT: return json_error(r,"409 Conflict","{\"error\":\"数据已改变，请刷新后重试\"}");
        case BAT_FULL: return json_error(r,"409 Conflict","{\"error\":\"已达到 16 个资产上限\"}");
        case BAT_NOT_FOUND: return json_error(r,"404 Not Found","{\"error\":\"资产不存在\"}");
        case BAT_TRANSITION: return json_error(r,"409 Conflict","{\"error\":\"当前状态不能执行此操作\"}");
        default: return json_error(r,"400 Bad Request","{\"error\":\"字段无效，请检查字节长度和数值范围\"}");
    }
}
static esp_err_t upsert(httpd_req_t *r) {
    cJSON *o=body(r); if (!o) return ESP_OK;
    bat_asset_t a={0}; double id,rev,capacity,cycles,soc,health,status,chemistry;
    bool valid=number(o,"id",0,UINT32_MAX,&id) && number(o,"revision",0,UINT32_MAX,&rev)
        && number(o,"capacity",1,60000,&capacity) && number(o,"cycles",0,65535,&cycles)
        && number(o,"soc",0,100,&soc) && number(o,"health",0,100,&health)
        && number(o,"status",0,BAT_STATUS_COUNT-1,&status) && number(o,"chemistry",0,3,&chemistry)
        && text_field(o,"name",a.name,sizeof(a.name)) && text_field(o,"location",a.location,sizeof(a.location))
        && text_field(o,"notes",a.notes,sizeof(a.notes));
    cJSON_Delete(o);
    if (!valid) return mutation_result(r,ESP_OK,BAT_INVALID);
    a.id=id; a.capacity_mah=capacity; a.cycles=cycles; a.soc=soc; a.health=health; a.status=status; a.chemistry=chemistry;
    bat_result_t result; esp_err_t err=battery_store_upsert(&a,rev,&result);
    return mutation_result(r,err,result);
}
static esp_err_t action(httpd_req_t *r) {
    cJSON *o=body(r); if (!o) return ESP_OK;
    double id,rev,a;
    bool valid=number(o,"id",1,UINT32_MAX,&id) && number(o,"revision",0,UINT32_MAX,&rev)
        && number(o,"action",BAT_CHECKOUT,BAT_RELEASE,&a);
    cJSON_Delete(o); if (!valid) return mutation_result(r,ESP_OK,BAT_INVALID);
    bat_result_t result; esp_err_t err=battery_store_action(id,(bat_action_t)a,rev,&result);
    return mutation_result(r,err,result);
}
static esp_err_t sync_time(httpd_req_t *r) {
    cJSON *o=body(r); if (!o) return ESP_OK;
    double epoch,tz;
    bool valid=number(o,"epoch",BAT_MIN_EPOCH,BAT_MAX_EPOCH,&epoch)
        && number(o,"timezone",-720,840,&tz);
    cJSON_Delete(o);
    if (!valid || !battery_time_sync((int64_t)epoch,(int)tz)) return mutation_result(r,ESP_OK,BAT_INVALID);
    return json_error(r,"200 OK","{\"ok\":true}");
}
bool battery_web_running(void) { return s_server!=NULL; }
const char *battery_web_ssid(void) { return s_ssid; }
const char *battery_web_password(void) { return s_password; }
void battery_web_stop(void) {
    if (s_server) { httpd_stop(s_server); s_server=NULL; }
    if (s_wifi_started) { esp_wifi_stop(); s_wifi_started=false; }
    if (s_wifi_init) { esp_wifi_deinit(); s_wifi_init=false; }
    if (s_netif) { esp_netif_destroy_default_wifi(s_netif); s_netif=NULL; }
    memset(s_password,0,sizeof(s_password));
}
esp_err_t battery_web_start(void) {
    if (s_server) return ESP_OK;
    esp_err_t err;
    if (!s_network_ready) {
        err=esp_netif_init(); if (err!=ESP_OK) return err;
        err=esp_event_loop_create_default(); if (err!=ESP_OK && err!=ESP_ERR_INVALID_STATE) return err;
        s_network_ready=true;
    }
    /* The default AP netif helper asserts on OOM. Build/attach explicitly instead. */
    esp_netif_config_t netif_config=ESP_NETIF_DEFAULT_WIFI_AP();
    s_netif=esp_netif_new(&netif_config);
    if (!s_netif) return ESP_ERR_NO_MEM;
    err=esp_netif_attach_wifi_ap(s_netif); if (err!=ESP_OK) goto fail;
    err=esp_wifi_set_default_wifi_ap_handlers(); if (err!=ESP_OK) goto fail;
    wifi_init_config_t init=WIFI_INIT_CONFIG_DEFAULT();
    err=esp_wifi_init(&init); if (err!=ESP_OK) goto fail;
    s_wifi_init=true;
    uint8_t mac[6]; err=esp_read_mac(mac,ESP_MAC_WIFI_SOFTAP); if (err!=ESP_OK) goto fail;
    snprintf(s_ssid,sizeof(s_ssid),"BatteryDesk-%02X%02X",mac[4],mac[5]);
    /* Generate after radio initialization: secure entropy source is now available. */
    static const char alphabet[]="ABCDEFGHJKLMNPQRSTUVWXYZ23456789";
    uint8_t random[12]; esp_fill_random(random,sizeof(random));
    for (unsigned i=0;i<sizeof(random);++i) s_password[i]=alphabet[random[i]&31];
    s_password[12]=0;
    wifi_config_t config={0};
    strcpy((char *)config.ap.ssid,s_ssid); strcpy((char *)config.ap.password,s_password);
    config.ap.ssid_len=strlen(s_ssid); config.ap.channel=1; config.ap.max_connection=1;
    config.ap.authmode=WIFI_AUTH_WPA2_PSK; config.ap.pmf_cfg.capable=true;
    err=esp_wifi_set_storage(WIFI_STORAGE_RAM); if (err!=ESP_OK) goto fail;
    err=esp_wifi_set_mode(WIFI_MODE_AP); if (err!=ESP_OK) goto fail;
    err=esp_wifi_set_config(WIFI_IF_AP,&config); if (err!=ESP_OK) goto fail;
    err=esp_wifi_start(); if (err!=ESP_OK) goto fail;
    s_wifi_started=true;
    esp_fill_random(random,sizeof(random));
    for (unsigned i=0;i<sizeof(random);++i) s_password[i]=alphabet[random[i]&31];
    strcpy((char *)config.ap.password,s_password);
    err=esp_wifi_set_config(WIFI_IF_AP,&config); if (err!=ESP_OK) goto fail;
    httpd_config_t http=HTTPD_DEFAULT_CONFIG();
    http.stack_size=7168; http.max_open_sockets=3; http.backlog_conn=2;
    http.lru_purge_enable=true; http.recv_wait_timeout=3; http.send_wait_timeout=5;
    err=httpd_start(&s_server,&http); if (err!=ESP_OK) goto fail;
    const httpd_uri_t routes[]={
        {.uri="/",.method=HTTP_GET,.handler=page},
        {.uri="/api/state",.method=HTTP_GET,.handler=state},
        {.uri="/api/assets",.method=HTTP_POST,.handler=upsert},
        {.uri="/api/action",.method=HTTP_POST,.handler=action},
        {.uri="/api/time",.method=HTTP_POST,.handler=sync_time}
    };
    for (unsigned i=0;i<sizeof(routes)/sizeof(routes[0]);++i) {
        err=httpd_register_uri_handler(s_server,&routes[i]); if (err!=ESP_OK) goto fail;
    }
    ESP_LOGI(TAG,"Web ready; heap=%u largest=%u",(unsigned)esp_get_free_heap_size(),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
    return ESP_OK;
fail:
    ESP_LOGE(TAG,"Start failed: %s",esp_err_to_name(err)); battery_web_stop(); return err;
}
