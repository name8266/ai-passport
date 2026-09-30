#pragma once
#include "esp_err.h"
#include <stdbool.h>

/* App-owned Wi-Fi/HTTP lifecycle, serialized by the UI worker. */
esp_err_t battery_web_start(void);
void battery_web_stop(void);
bool battery_web_running(void);
const char *battery_web_ssid(void);
const char *battery_web_password(void);
