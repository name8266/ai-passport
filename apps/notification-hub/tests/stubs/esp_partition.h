#pragma once
#include <stddef.h>
#include <string.h>
#define ESP_OK 0
#define ESP_PARTITION_TYPE_DATA 1
#define ESP_PARTITION_SUBTYPE_DATA_FAT 2
typedef int esp_err_t;
typedef struct {size_t size;size_t address;} esp_partition_t;
static bool test_blank=true;
static size_t test_partition_address=0x400000;
static int test_erase_error;
static inline const esp_partition_t *esp_partition_find_first(int type,int sub,const char *name) {
    (void)type;(void)sub;(void)name;static esp_partition_t p={4*1024*1024,0x400000};p.address=test_partition_address;return &p;
}
static inline int esp_partition_read(const esp_partition_t *p,size_t off,void *buffer,size_t size) {
    (void)p;(void)off;memset(buffer,test_blank?0xff:0,size);return 0;
}
static inline const char *esp_err_to_name(int err) {(void)err;return "test";}

static int test_erases;
static inline int esp_partition_erase_range(const esp_partition_t *p,size_t off,size_t size) {
    assert(p->address==0x400000 && off==0 && size==0x400000);
    if(test_erase_error) return test_erase_error;
    test_erases++;test_blank=true;
    /* Erase the files representing this simulated FAT volume. */
    remove(HUB_ARCHIVE_PATH);remove("digest.dat");
    remove("alerts.new");remove("swap.ok");return 0;
}
