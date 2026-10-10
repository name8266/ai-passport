#include "hub_web_auth.h"
#include "hub_form.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    static const char token[]="0123456789abcdef0123456789abcdef";
    assert(hub_web_auth_credentials_match("admin","admin","admin","admin"));
    assert(!hub_web_auth_credentials_match("admin","wrong","admin","admin"));
    assert(!hub_web_auth_credentials_match("user","admin","admin","admin"));
    assert(!hub_web_auth_credentials_match(NULL,"admin","admin","admin"));
    assert(hub_web_auth_cookie_matches("notifyhub=0123456789abcdef0123456789abcdef",token));
    assert(hub_web_auth_cookie_matches("theme=dark; notifyhub=0123456789abcdef0123456789abcdef; x=y",token));
    assert(hub_web_auth_cookie_matches("notifyhub=0123456789abcdef0123456789abcdef \t",token));
    assert(!hub_web_auth_cookie_matches("other=0123456789abcdef0123456789abcdef",token));
    assert(!hub_web_auth_cookie_matches("notifyhub=0123456789abcdef0123456789abcdefx",token));
    assert(!hub_web_auth_cookie_matches("xnotifyhub=0123456789abcdef0123456789abcdef",token));
    assert(!hub_web_auth_cookie_matches(NULL,token));
    char user[16],password[16];
    assert(hub_form_parse_login("username=admin&password=admin",user,sizeof(user),password,sizeof(password)));
    assert(!strcmp(user,"admin") && !strcmp(password,"admin"));
    assert(hub_form_parse_login("username=admin&password=a%26b",user,sizeof(user),password,sizeof(password)));
    assert(!strcmp(password,"a&b"));
    assert(!hub_form_parse_login("username=admin&password=bad%00value",user,sizeof(user),password,sizeof(password)));
    assert(!hub_form_parse_login("username=admin&password=waytoolong",user,sizeof(user),password,4));
    puts("Portal login form and session-cookie matching: PASS");
}
