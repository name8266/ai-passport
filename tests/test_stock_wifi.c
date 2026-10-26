#include "stock_wifi.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    stock_wifi_credentials_t saved = {.ssid = "saved-network", .password = "test-password"};
    stock_wifi_credentials_t builtin = {.ssid = "default-network", .password = "unit-test-only"};
    stock_wifi_credentials_t out;
    assert(stock_wifi_choose(&out, &saved, &builtin) == STOCK_WIFI_SAVED);
    assert(!strcmp(out.ssid, saved.ssid) && !strcmp(out.password, saved.password));
    assert(!stock_wifi_try_builtin_after_retries(STOCK_WIFI_SAVED, 2, &builtin));
    assert(stock_wifi_try_builtin_after_retries(STOCK_WIFI_SAVED, 3, &builtin));
    assert(!stock_wifi_try_builtin_after_retries(STOCK_WIFI_BUILTIN, 10, &builtin));
    assert(stock_wifi_choose(&out, NULL, &builtin) == STOCK_WIFI_BUILTIN);
    assert(!strcmp(out.ssid, builtin.ssid) && !strcmp(out.password, builtin.password));
    saved.password[0] = 0; // Explicitly saved open network overrides defaults.
    assert(stock_wifi_choose(&out, &saved, &builtin) == STOCK_WIFI_SAVED && !out.password[0]);
    strcpy(saved.password, "short");
    assert(stock_wifi_choose(&out, &saved, &builtin) == STOCK_WIFI_BUILTIN);
    memset(saved.ssid, 's', sizeof(saved.ssid)); // Unterminated NVS input is rejected.
    assert(stock_wifi_choose(&out, &saved, &builtin) == STOCK_WIFI_BUILTIN);
    memset(saved.ssid, 's', sizeof(saved.ssid) - 1);
    saved.ssid[32] = 0;
    memset(saved.password, 'p', sizeof(saved.password) - 1);
    saved.password[63] = 0;
    assert(stock_wifi_choose(&out, &saved, &builtin) == STOCK_WIFI_SAVED);
    assert(strlen(out.ssid) == 32 && strlen(out.password) == 63);
    memset(saved.password, 'x', sizeof(saved.password));
    assert(stock_wifi_choose(&out, &saved, &builtin) == STOCK_WIFI_BUILTIN);
    builtin.ssid[0] = 0;
    assert(!stock_wifi_try_builtin_after_retries(STOCK_WIFI_SAVED, 10, &builtin));
    memset(&out, 0x5a, sizeof(out));
    assert(stock_wifi_choose(&out, &saved, &builtin) == STOCK_WIFI_NONE);
    stock_wifi_credentials_t empty = {0};
    assert(!memcmp(&out, &empty, sizeof(out)));
    assert(stock_wifi_choose(NULL, &saved, &builtin) == STOCK_WIFI_NONE);
    assert(stock_wifi_choose(&out, NULL, stock_wifi_builtin()) == STOCK_WIFI_NONE);
    puts("Stock Wi-Fi: PASS (saved/default selection, open network, missing/corrupt credentials, bounds)");
}
