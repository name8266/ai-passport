#include "hub_web_auth.h"
#include <stddef.h>
#include <string.h>

static bool equal_n(const char *a,size_t an,const char *b,size_t bn) {
    size_t max=an>bn?an:bn;
    unsigned int diff=(unsigned int)(an^bn);
    for(size_t i=0;i<max;i++) {
        unsigned char ac=i<an?(unsigned char)a[i]:0;
        unsigned char bc=i<bn?(unsigned char)b[i]:0;
        diff|=ac^bc;
    }
    return diff==0;
}

bool hub_web_auth_credentials_match(const char *username,const char *password,
                                    const char *expected_username,
                                    const char *expected_password) {
    if(!username || !password || !expected_username || !expected_password)return false;
    size_t user_len=strlen(username),expected_user_len=strlen(expected_username);
    size_t pass_len=strlen(password),expected_pass_len=strlen(expected_password);
    return equal_n(username,user_len,expected_username,expected_user_len) &&
           equal_n(password,pass_len,expected_password,expected_pass_len);
}

bool hub_web_auth_cookie_matches(const char *cookie_header,const char *token) {
    if(!cookie_header || !token || !token[0])return false;
    const char *p=cookie_header;
    while(*p) {
        while(*p==' ' || *p=='\t' || *p==';')p++;
        const char *end=strchr(p,';');
        if(!end)end=p+strlen(p);
        const char *trim=end;
        while(trim>p && (trim[-1]==' ' || trim[-1]=='\t'))trim--;
        const char *eq=memchr(p,'=',(size_t)(trim-p));
        if(eq && equal_n(p,(size_t)(eq-p),"notifyhub",9)) {
            const char *value=eq+1;
            return equal_n(value,(size_t)(trim-value),token,strlen(token));
        }
        p=*end?end+1:end;
    }
    return false;
}
