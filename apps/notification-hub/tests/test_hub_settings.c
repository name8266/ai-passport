#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../firmware/main/hub_ai_settings.c"
static int open_error,write_error,commits;
static const char *saved_endpoint;
int nvs_open(const char *n,int mode,nvs_handle_t *h) {(void)n;(void)mode;*h=1;return open_error;}
void nvs_close(nvs_handle_t h) {(void)h;}
int nvs_commit(nvs_handle_t h) {(void)h;commits++;return write_error;}
int nvs_get_str(nvs_handle_t h,const char *k,char *d,size_t *n) {
    (void)h;
    if(strcmp(k,"endpoint") || !saved_endpoint) return 1;
    size_t size=strlen(saved_endpoint)+1;
    if(d && *n<size)return 2;
    *n=size;
    if(d)memcpy(d,saved_endpoint,size);
    return 0;
}
int nvs_set_str(nvs_handle_t h,const char *k,const char *v) {(void)h;(void)k;(void)v;return write_error;}
#define STUB_INTEGER(suffix,type) \
int nvs_get_##suffix(nvs_handle_t h,const char *k,type *v) {(void)h;(void)k;(void)v;return 1;} \
int nvs_set_##suffix(nvs_handle_t h,const char *k,type v) {(void)h;(void)k;(void)v;return write_error;}
STUB_INTEGER(u8,uint8_t)
STUB_INTEGER(u16,uint16_t)
STUB_INTEGER(u32,uint32_t)
int main(void) {
    hub_ai_settings_t s;
    open_error=1;assert(hub_ai_read_settings(&s));
    assert(!s.enabled && !strcmp(s.endpoint,HUB_AI_DEFAULT_ENDPOINT));
    assert(s.retention_days==30 && s.brightness==80);
    /* Namespace exists after generating admin credentials, but AI keys don't. */
    open_error=0;assert(hub_ai_read_settings(&s));
    assert(!s.enabled && !strcmp(s.endpoint,HUB_AI_DEFAULT_ENDPOINT));
    assert(s.retention_days==30 && s.brightness==80);
    assert(!strcmp(s.model,HUB_AI_DEFAULT_MODEL));
    saved_endpoint="https://example.com/chat";
    assert(hub_ai_read_settings(&s) && !strcmp(s.endpoint,saved_endpoint));
    s.retention_days=90;s.brightness=60;
    hub_ai_connection_t wifi={0};
    assert(hub_ai_save_settings(&s,&wifi) && commits==1);
    s.enabled=true;assert(!hub_ai_save_settings(&s,&wifi));
    s.enabled=false;s.retention_days=0;assert(!hub_ai_save_settings(&s,&wifi));
    s.retention_days=30;s.brightness=10;assert(!hub_ai_save_settings(&s,&wifi));
    s.brightness=80;
    strcpy(s.api_key,"test-key");write_error=1;
    assert(!hub_ai_save_settings(&s,&wifi) && commits==1);
    saved_endpoint="http://example.com/chat";assert(!hub_ai_read_settings(&s));
    write_error=0;assert(hub_ai_reset_cursor());
    write_error=1;assert(!hub_ai_reset_cursor());
    puts("AI first-boot defaults, validation and failed writes: PASS");
}
