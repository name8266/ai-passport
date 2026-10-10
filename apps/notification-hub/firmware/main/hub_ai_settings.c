#include "hub_ai.h"
#include <stdio.h>
#include <string.h>
#include "nvs.h"

static bool read_str(nvs_handle_t h,const char *key,char *dest,size_t bytes) {
    size_t n=bytes;
    /* A missing key must retain the caller's initialized default. */
    esp_err_t err=nvs_get_str(h,key,NULL,&n);
    if(err!=ESP_OK || n>bytes) return false;
    return nvs_get_str(h,key,dest,&n)==ESP_OK;
}
static bool endpoint_ok(const char *url) {
    if(!url || strncmp(url,"https://",8)!=0 || strlen(url)>180) return false;
    const char *host=url+8;
    const char *path=strchr(host,'/');
    if(host[0]==0 || host[0]=='/' || host[0]=='@') return false;
    if(!path || path==host || !path[1]) return false;
    for(const char *p=url;*p;p++)
        if(*p==' ' || *p=='\r' || *p=='\n' || *p=='@' ||
           *p=='#' || *p=='?') return false;
    if(strstr(host,"127.0.0.1") || strstr(host,"localhost") ||
       strstr(host,"192.168.") || strstr(host,"10.0.")) return false;
    return true;
}
static void default_settings(hub_ai_settings_t *s) {
    memset(s,0,sizeof(*s));
    s->enabled=false;
    s->redact_sensitive=true;
    s->interval_minutes=60;
    s->max_records=HUB_AI_MAX_BATCH;
    s->retention_days=30;
    s->brightness=80;
    snprintf(s->endpoint,sizeof(s->endpoint),"%s",HUB_AI_DEFAULT_ENDPOINT);
    snprintf(s->model,sizeof(s->model),"%s",HUB_AI_DEFAULT_MODEL);
    s->theme=0;
    s->sound=hub_sound_defaults();
}
bool hub_ai_load_connection(hub_ai_connection_t *c) {
    if(!c) return false;
    memset(c,0,sizeof(*c));
    nvs_handle_t h;
    if(nvs_open("hub_ai",NVS_READONLY,&h)!=ESP_OK) return false;
    bool got_ssid=read_str(h,"ssid",c->ssid,sizeof(c->ssid));
    bool got_pass=read_str(h,"pass",c->password,sizeof(c->password));
    nvs_close(h);
    c->configured=got_ssid && got_pass && c->ssid[0] && c->password[0];
    return c->configured;
}
bool hub_ai_read_settings(hub_ai_settings_t *out) {
    if(!out) return false;
    default_settings(out);
    nvs_handle_t h;
    if(nvs_open("hub_ai",NVS_READONLY,&h)!=ESP_OK) return true;
    uint8_t active=0, redact=1, theme=0, sound_enabled=0, sound_tone=0,
            sound_volume=0;
    uint16_t minutes=60, retention=30;
    uint8_t count=HUB_AI_MAX_BATCH, brightness=80;
    (void)nvs_get_u8(h,"enabled",&active);
    (void)nvs_get_u8(h,"redact",&redact);
    (void)nvs_get_u8(h,"theme",&theme);
    esp_err_t sound_on_err=nvs_get_u8(h,"sound_on",&sound_enabled);
    esp_err_t sound_tone_err=nvs_get_u8(h,"sound_tone",&sound_tone);
    esp_err_t sound_volume_err=nvs_get_u8(h,"sound_volume",&sound_volume);
    (void)nvs_get_u16(h,"interval",&minutes);
    (void)nvs_get_u16(h,"retention",&retention);
    (void)nvs_get_u8(h,"max_items",&count);
    (void)nvs_get_u8(h,"brightness",&brightness);
    (void)read_str(h,"endpoint",out->endpoint,sizeof(out->endpoint));
    (void)read_str(h,"model",out->model,sizeof(out->model));
    (void)read_str(h,"api_key",out->api_key,sizeof(out->api_key));
    (void)read_str(h,"exclude",out->excluded_apps,sizeof(out->excluded_apps));
    nvs_close(h);
    if(!endpoint_ok(out->endpoint) || !out->model[0]) return false;
    out->interval_minutes=minutes>=15 && minutes<=1440?minutes:60;
    out->max_records=count>=1&&count<=HUB_AI_MAX_BATCH?count:HUB_AI_MAX_BATCH;
    out->retention_days=retention>=1&&retention<=365?retention:30;
    out->brightness=brightness>=20&&brightness<=100?brightness:80;
    out->redact_sensitive=redact!=0;
    out->theme=theme<=1?theme:0;
    if(sound_on_err==ESP_OK && sound_tone_err==ESP_OK && sound_volume_err==ESP_OK) {
        hub_sound_config_t stored={sound_enabled!=0,sound_tone,sound_volume};
        if(hub_sound_valid(stored)) out->sound=stored;
    } else {
        /* Read v1 settings left by earlier firmware until the user next saves. */
        nvs_handle_t old;
        uint32_t bits=0;
        if(nvs_open("hub_sound",NVS_READONLY,&old)==ESP_OK) {
            if(nvs_get_u32(old,"config",&bits)==ESP_OK) {
                hub_sound_config_t stored={(bits&1u)!=0,(uint8_t)(bits>>8),
                                            (uint8_t)(bits>>16)};
                if(hub_sound_valid(stored)) out->sound=stored;
            }
            nvs_close(old);
        }
    }
    out->enabled=active!=0 && out->api_key[0]!=0;
    return true;
}
uint8_t hub_ai_read_theme(void) {
    nvs_handle_t h;uint8_t theme=0;
    if(nvs_open("hub_ai",NVS_READONLY,&h)==ESP_OK) {
        (void)nvs_get_u8(h,"theme",&theme);
        nvs_close(h);
    }
    return theme<=1?theme:0;
}
uint8_t hub_ai_read_brightness(void) {
    nvs_handle_t h;uint8_t brightness=80;
    if(nvs_open("hub_ai",NVS_READONLY,&h)==ESP_OK) {
        (void)nvs_get_u8(h,"brightness",&brightness);
        nvs_close(h);
    }
    return brightness>=20&&brightness<=100?brightness:80;
}
bool hub_ai_save_settings(const hub_ai_settings_t *s,const hub_ai_connection_t *wifi) {
    if(!s || !wifi || !endpoint_ok(s->endpoint) || !s->model[0] ||
       strlen(s->model)>=sizeof(s->model) ||
       strlen(s->api_key)>=sizeof(s->api_key) ||
       s->interval_minutes<15 || s->interval_minutes>1440 ||
       s->max_records<1 || s->max_records>HUB_AI_MAX_BATCH ||
       s->retention_days<1 || s->retention_days>365 ||
       s->brightness<20 || s->brightness>100 ||
       (s->enabled && !s->api_key[0]) ||
       strlen(wifi->ssid)>32 || strlen(wifi->password)>64 || s->theme>1 ||
       !hub_sound_valid(s->sound)) return false;
    if((wifi->ssid[0] && !wifi->password[0]) ||
       (!wifi->ssid[0] && wifi->password[0])) return false;
    nvs_handle_t h;
    if(nvs_open("hub_ai",NVS_READWRITE,&h)!=ESP_OK) return false;
    esp_err_t err=ESP_OK;
#define SET_OR_ERROR(call) do {if(err==ESP_OK)err=(call);}while(0)
    SET_OR_ERROR(nvs_set_u8(h,"enabled",s->enabled?1:0));
    SET_OR_ERROR(nvs_set_u8(h,"redact",s->redact_sensitive?1:0));
    SET_OR_ERROR(nvs_set_u16(h,"interval",(uint16_t)s->interval_minutes));
    SET_OR_ERROR(nvs_set_u16(h,"retention",s->retention_days));
    SET_OR_ERROR(nvs_set_u8(h,"max_items",(uint8_t)s->max_records));
    SET_OR_ERROR(nvs_set_u8(h,"brightness",s->brightness));
    SET_OR_ERROR(nvs_set_str(h,"endpoint",s->endpoint));
    SET_OR_ERROR(nvs_set_str(h,"model",s->model));
    SET_OR_ERROR(nvs_set_str(h,"api_key",s->api_key));
    SET_OR_ERROR(nvs_set_str(h,"exclude",s->excluded_apps));
    SET_OR_ERROR(nvs_set_u8(h,"theme",s->theme));
    SET_OR_ERROR(nvs_set_u8(h,"sound_on",s->sound.enabled?1:0));
    SET_OR_ERROR(nvs_set_u8(h,"sound_tone",s->sound.tone));
    SET_OR_ERROR(nvs_set_u8(h,"sound_volume",s->sound.volume));
    if(wifi->ssid[0]) {
        SET_OR_ERROR(nvs_set_str(h,"ssid",wifi->ssid));
        SET_OR_ERROR(nvs_set_str(h,"pass",wifi->password));
    }
    if(err==ESP_OK)err=nvs_commit(h);
#undef SET_OR_ERROR
    nvs_close(h);
    return err==ESP_OK;
}
uint32_t hub_ai_read_cursor(void) {
    uint32_t value=0;
    nvs_handle_t h;
    if(nvs_open("hub_ai",NVS_READONLY,&h)==ESP_OK) {
        (void)nvs_get_u32(h,"cursor",&value);
        nvs_close(h);
    }
    return value;
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
bool hub_ai_set_cursor(uint32_t value) {
    nvs_handle_t h;
    if(nvs_open("hub_ai",NVS_READWRITE,&h)!=ESP_OK) return false;
    esp_err_t err=nvs_set_u32(h,"cursor",value);
    if(err==ESP_OK) err=nvs_commit(h);
    nvs_close(h);
    return err==ESP_OK;
}
bool hub_ai_reset_cursor(void) {return hub_ai_set_cursor(0);}
