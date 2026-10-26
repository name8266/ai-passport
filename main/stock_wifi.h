#pragma once
#include <stdbool.h>

typedef struct {
    char ssid[33];
    char password[64];
} stock_wifi_credentials_t;

typedef enum { STOCK_WIFI_NONE, STOCK_WIFI_SAVED, STOCK_WIFI_BUILTIN } stock_wifi_source_t;

/* Saved credentials take precedence, allowing the owner to change networks. */
stock_wifi_source_t stock_wifi_choose(stock_wifi_credentials_t *out,
                                     const stock_wifi_credentials_t *saved,
                                     const stock_wifi_credentials_t *builtin);
const stock_wifi_credentials_t *stock_wifi_builtin(void);
bool stock_wifi_try_builtin_after_retries(stock_wifi_source_t current,
                                         unsigned failed_attempts,
                                         const stock_wifi_credentials_t *builtin);
