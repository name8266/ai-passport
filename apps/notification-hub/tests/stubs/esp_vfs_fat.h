#pragma once
#include <stdbool.h>
#include <stddef.h>
#include "wear_levelling.h"
typedef struct {bool format_if_mount_failed;int max_files;size_t allocation_unit_size;} esp_vfs_fat_mount_config_t;
static bool test_mount_error,test_format;
static int test_unmount_error;
static inline int esp_vfs_fat_spiflash_mount_rw_wl(const char *path,const char *part,const esp_vfs_fat_mount_config_t *cfg,wl_handle_t *wl) {
    /* Match IDF: a failed FAT mount may leave a live WL handle. */
    (void)path;(void)part;*wl=1;test_format=cfg->format_if_mount_failed;return test_mount_error?1:0;
}

static inline int esp_vfs_fat_spiflash_unmount_rw_wl(const char *path,wl_handle_t wl) {(void)path;(void)wl;return test_unmount_error;}
