#include "hub_form.h"
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <limits.h>
static int hex_digit(char c) {
    if(c>='0'&&c<='9')return c-'0';
    if(c>='a'&&c<='f')return c-'a'+10;
    if(c>='A'&&c<='F')return c-'A'+10;
    return -1;
}
static bool decode_component(const char *src,size_t n,char *dst,size_t cap) {
    size_t j=0;
    if(cap==0)return false;
    for(size_t i=0;i<n;i++) {
        uint8_t value=(uint8_t)src[i];
        if(value=='+') value=' ';
        else if(value=='%') {
            if(i+2>=n) return false;
            int hi=hex_digit(src[++i]),lo=hex_digit(src[++i]);
            if(hi<0||lo<0)return false;
            value=(uint8_t)((hi<<4)|lo);
        }
        if(value==0 || value=='\r' || value=='\n' ||
           (value<32 || value==127) || j+1>=cap)return false;
        dst[j++]=(char)value;
    }
    dst[j]=0;
    return true;
}
static bool field(const char *form,const char *key,char *out,size_t cap) {
    if(cap==0)return false;
    out[0]=0;
    size_t target=strlen(key);
    const char *p=form;
    while(p && *p) {
        const char *end=strchr(p,'&');
        if(!end)end=p+strlen(p);
        const char *eq=memchr(p,'=',(size_t)(end-p));
        if(eq && (size_t)(eq-p)==target && memcmp(p,key,target)==0)
            return decode_component(eq+1,(size_t)(end-eq-1),out,cap);
        p=*end?end+1:NULL;
    }
    return false;
}

static bool number(const char *body,const char *key,int *out) {
    char text[16], *end;
    if(!field(body,key,text,sizeof(text)) || !text[0]) return false;
    errno=0;
    long value=strtol(text,&end,10);
    if(errno || *end || value<0 || value>INT_MAX) return false;
    *out=(int)value;
    return true;
}
bool hub_form_parse(const char *body,const char *nonce,
                    hub_ai_settings_t *cfg,hub_ai_connection_t *wifi) {
    char token[32],password[65],api[192],enabled[4],redact[4],
         sound_enabled[4],theme[8];
    int tone,volume,retention,brightness;
    if(!body || !nonce || !nonce[0] || !cfg || !wifi ||
       !field(body,"csrf",token,sizeof(token)) || strcmp(token,nonce)!=0 ||
       !field(body,"ssid",wifi->ssid,sizeof(wifi->ssid)) ||
       !field(body,"wifi_password",password,sizeof(password)) ||
       !field(body,"endpoint",cfg->endpoint,sizeof(cfg->endpoint)) ||
       !field(body,"model",cfg->model,sizeof(cfg->model)) ||
       !field(body,"api_key",api,sizeof(api)) ||
       !field(body,"excluded_apps",cfg->excluded_apps,sizeof(cfg->excluded_apps)) ||
       !field(body,"theme",theme,sizeof(theme)) ||
       !number(body,"interval",&cfg->interval_minutes) ||
       !number(body,"max_records",&cfg->max_records) ||
       !number(body,"retention_days",&retention) ||
       !number(body,"brightness",&brightness) ||
       !number(body,"sound_tone",&tone) ||
       !number(body,"sound_volume",&volume)) return false;
    if(password[0]) memcpy(wifi->password,password,strlen(password)+1);
    if(api[0]) memcpy(cfg->api_key,api,strlen(api)+1);
    cfg->enabled=field(body,"enabled",enabled,sizeof(enabled)) && !strcmp(enabled,"1");
    cfg->redact_sensitive=field(body,"redact_sensitive",redact,sizeof(redact)) && !strcmp(redact,"1");
    if(strcmp(theme,"light") && strcmp(theme,"dark")) return false;
    cfg->theme=!strcmp(theme,"dark");
    cfg->sound.enabled=field(body,"sound_enabled",sound_enabled,
                             sizeof(sound_enabled)) && !strcmp(sound_enabled,"1");
    if(tone>UINT8_MAX || volume>UINT8_MAX || retention>UINT16_MAX ||
       brightness>UINT8_MAX || retention<1 || retention>365 ||
       brightness<20 || brightness>100) return false;
    cfg->retention_days=(uint16_t)retention;
    cfg->brightness=(uint8_t)brightness;
    cfg->sound.tone=(uint8_t)tone;
    cfg->sound.volume=(uint8_t)volume;
    return cfg->sound.tone<HUB_TONE_COUNT && cfg->sound.volume>=10 &&
           cfg->sound.volume<=60 && (wifi->ssid[0] || !cfg->enabled);
}
bool hub_form_confirm_archive_clear(const char *body,const char *nonce) {
    char token[32],confirmation[16];
    return body && nonce && nonce[0] &&
        field(body,"csrf",token,sizeof(token)) && !strcmp(token,nonce) &&
        field(body,"confirm",confirmation,sizeof(confirmation)) &&
        !strcmp(confirmation,"CLEAR");
}
bool hub_form_validate_csrf(const char *body,const char *nonce) {
    char token[32];
    return body && nonce && nonce[0] && field(body,"csrf",token,sizeof(token)) &&
           strcmp(token,nonce)==0;
}
bool hub_form_parse_login(const char *body,char *username,size_t username_size,
                          char *password,size_t password_size) {
    return body && username && password &&
        field(body,"username",username,username_size) &&
        field(body,"password",password,password_size);
}

bool hub_form_parse_control(const char *body,const char *nonce,unsigned *action) {
    int value;
    if(!action || !hub_form_validate_csrf(body,nonce) ||
       !number(body,"action",&value) || value>6) return false;
    *action=(unsigned)value;
    return true;
}
