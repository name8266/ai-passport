#pragma once
#include "nvs.h"
esp_err_t nvs_flash_init(void);
esp_err_t nvs_flash_deinit(void);
const char *esp_err_to_name(esp_err_t err);
