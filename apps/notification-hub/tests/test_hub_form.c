#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "hub_form.h"
static const char *prefix="csrf=test&ssid=home&wifi_password=&endpoint=https%3A%2F%2Fexample.com%2Fchat&api_key=&excluded_apps=&theme=light&brightness=80&retention_days=30&sound_tone=0&sound_volume=30&interval=60&max_records=3&model=";
static bool parse(const char *suffix) {
    char body[2048];snprintf(body,sizeof(body),"%s%s",prefix,suffix);
    hub_ai_settings_t s={0};hub_ai_connection_t w={0};
    strcpy(s.api_key,"keep-key");strcpy(w.password,"keep-password");
    bool ok=hub_form_parse(body,"test",&s,&w);
    if(ok) {assert(!strcmp(s.api_key,"keep-key"));assert(!strcmp(w.password,"keep-password"));}
    return ok;
}
int main(void) {
    unsigned action=99;
    for(unsigned i=0;i<=6;i++) {
        char command[64];snprintf(command,sizeof(command),"csrf=token&action=%u",i);
        assert(hub_form_parse_control(command,"token",&action) && action==i);
    }
    assert(!hub_form_parse_control("csrf=wrong&action=0","token",&action));
    assert(!hub_form_parse_control("csrf=token&action=7","token",&action));
    assert(!hub_form_parse_control("csrf=token&action=-1","token",&action));
    assert(!hub_form_parse_control("csrf=token&action=1oops","token",&action));
    assert(!hub_form_parse_control("csrf=token","token",&action));

    assert(parse("model%2Bname&enabled=1&redact_sensitive=1&sound_enabled=1"));
    {
        char body[2048];snprintf(body,sizeof(body),"csrf=test&ssid=home&wifi_password=&endpoint=https%%3A%%2F%%2Fexample.com%%2Fchat&api_key=&excluded_apps=&theme=dark&brightness=60&retention_days=90&sound_tone=2&sound_volume=60&interval=60&max_records=3&model=model");
        hub_ai_settings_t s={0};hub_ai_connection_t w={0};
        assert(hub_form_parse(body,"test",&s,&w));
        assert(s.theme==1 && !s.sound.enabled && s.sound.tone==2 && s.sound.volume==60);
        assert(s.brightness==60 && s.retention_days==90);
    }
    assert(!parse("invalid%00model"));assert(!parse("bad%0D%0Aheader"));
    assert(!parse("bad%09tab"));assert(!parse("bad%xy"));
    char large[200];memset(large,'x',sizeof(large)-1);large[sizeof(large)-1]=0;
    assert(!parse(large));
    hub_ai_settings_t s={0};hub_ai_connection_t w={0};
    assert(!hub_form_parse("csrf=test","test",&s,&w));
    char body[2048];snprintf(body,sizeof(body),"%sx",prefix);
    assert(!hub_form_parse(body,"wrong",&s,&w));
    char *n=strstr(body,"interval=60");n[9]='x';
    assert(!hub_form_parse(body,"test",&s,&w));
    assert(hub_form_confirm_archive_clear("csrf=test&confirm=CLEAR","test"));
    assert(!hub_form_confirm_archive_clear("csrf=wrong&confirm=CLEAR","test"));
    assert(!hub_form_confirm_archive_clear("csrf=test&confirm=clear","test"));
    assert(hub_form_validate_csrf("csrf=test&x=1","test"));
    assert(!hub_form_validate_csrf("csrf=wrong","test"));
    {
        char body[2200];
        snprintf(body,sizeof(body),"%smodel&enabled=1&brightness=101",prefix);
        char *brightness=strstr(body,"brightness=80");assert(brightness);
        brightness[11]='1';brightness[12]='0'; /* invalidate the existing field, not a duplicate */
        assert(!hub_form_parse(body,"test",&s,&w));
        snprintf(body,sizeof(body),"%smodel&enabled=1&retention_days=0",prefix);
        char *retention=strstr(body,"retention_days=30");assert(retention);
        retention[15]='0';retention[16]='0';
        assert(!hub_form_parse(body,"test",&s,&w));
    }
    puts("Admin form malformed input, limits, nonce and retained secrets: PASS");
}
