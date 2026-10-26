#include "stock_wifi.h"
#include "stock_wifi_build.h"
#include <stddef.h>
#include <string.h>

static const stock_wifi_credentials_t defaults = {
    .ssid = STOCK_WIFI_BUILTIN_SSID, .password = STOCK_WIFI_BUILTIN_PASSWORD};

static bool valid(const stock_wifi_credentials_t *c) {
    if (!c || !c->ssid[0] || !memchr(c->ssid, 0, sizeof(c->ssid)))
        return false;
    const char *end = memchr(c->password, 0, sizeof(c->password));
    if (!end)
        return false;
    size_t length = (size_t)(end - c->password);
    return length == 0 || length >= 8;
}

stock_wifi_source_t stock_wifi_choose(stock_wifi_credentials_t *out,
                                     const stock_wifi_credentials_t *saved,
                                     const stock_wifi_credentials_t *builtin) {
    if (!out)
        return STOCK_WIFI_NONE;
    stock_wifi_credentials_t next = {0};
    stock_wifi_source_t source = STOCK_WIFI_NONE;
    if (valid(saved)) {
        next = *saved;
        source = STOCK_WIFI_SAVED;
    } else if (valid(builtin)) {
        next = *builtin;
        source = STOCK_WIFI_BUILTIN;
    }
    *out = next;
    return source;
}

const stock_wifi_credentials_t *stock_wifi_builtin(void) {
    return &defaults;
}

bool stock_wifi_try_builtin_after_retries(stock_wifi_source_t current,
                                         unsigned failed_attempts,
                                         const stock_wifi_credentials_t *builtin) {
    return current == STOCK_WIFI_SAVED && failed_attempts >= 3 && valid(builtin);
}
