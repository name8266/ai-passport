#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
/* Apply this to full archival text BEFORE reducing text for a compact batch. */
static inline bool hub_ai_sensitive(const char *title,const char *body) {
    const char *keys[]={"验证码","校验码","动态密码","一次性密码",
        "密码","银行卡","账单验证码","verification code",
        "OTP","passcode","security code"};
    for(size_t i=0;i<sizeof(keys)/sizeof(keys[0]);i++)
        if(strstr(title,keys[i])||strstr(body,keys[i])) return true;
    return false;
}
