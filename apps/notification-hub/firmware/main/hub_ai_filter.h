#pragma once
/* Lightweight fail-closed screening of common secrets BEFORE an external
 * AI request is constructed. Not a full DLP solution; data never leaves
 * without explicit AI enablement and user-configured HTTPS credentials. */
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
static inline unsigned char hub_ascii_lower(unsigned char c) {
    return c>='A' && c<='Z'?(unsigned char)(c+('a'-'A')):c;
}
static inline bool hub_secret_ascii_contains(const char *hay,
                                               const char *term,
                                               bool word) {
    if(!hay || !term || !term[0])return false;
    size_t width=strlen(term);
    for(size_t pos=0;hay[pos];pos++) {
        size_t i=0;
        for(;i<width && hay[pos+i];i++) {
            if(hub_ascii_lower((unsigned char)hay[pos+i]) !=
               hub_ascii_lower((unsigned char)term[i]))break;
        }
        if(i!=width)continue;
        if(word) {
            unsigned char prev=pos?(unsigned char)hay[pos-1]:0;
            unsigned char next=(unsigned char)hay[pos+width];
            bool pword=(prev>='a'&&prev<='z')||(prev>='A'&&prev<='Z');
            bool nword=(next>='a'&&next<='z')||(next>='A'&&next<='Z');
            if(pword || nword)continue;
        }
        return true;
    }
    return false;
}
static inline bool hub_ai_sensitive(const char *title,const char *body) {
    if(!title)title="";
    if(!body)body="";
    static const char *const zh[]={
        "验证码","校验码","动态密码","一次性密码","取款码",
        "交易密码","支付密码","短信口令","动态口令","银行卡号",
        "信用卡号","身份证号","重置密码","密码","银行卡",
        "安全码","安全验证码","私密口令"
    };
    static const char *const phrases[]={
        "verification code","security code","one-time","one time",
        "passcode","password","reset code","card number","bank card",
        "authentication code","temporary code","two-factor","2-factor"
    };
    static const char *const abbreviations[]={"otp","2fa","cvv","pin"};
    for(size_t i=0;i<sizeof(zh)/sizeof(zh[0]);i++)
        if(strstr(title,zh[i]) || strstr(body,zh[i]))return true;
    for(size_t i=0;i<sizeof(phrases)/sizeof(phrases[0]);i++)
        if(hub_secret_ascii_contains(title,phrases[i],false) ||
           hub_secret_ascii_contains(body,phrases[i],false))return true;
    for(size_t i=0;i<sizeof(abbreviations)/sizeof(abbreviations[0]);i++)
        if(hub_secret_ascii_contains(title,abbreviations[i],i>=2) ||
           hub_secret_ascii_contains(body,abbreviations[i],i>=2))return true;
    return false;
}
