#pragma once
#include "esp_err.h"
#include <stdbool.h>
#include <stddef.h>
extern const char *battery_test_root;
#define BAT_STORAGE_ROOT battery_test_root
typedef struct { const char *base_path,*partition_label; bool format_if_mount_failed; } esp_vfs_littlefs_conf_t;
esp_err_t esp_vfs_littlefs_register(const esp_vfs_littlefs_conf_t *config);
esp_err_t esp_littlefs_info(const char *label,size_t *total,size_t *used);
