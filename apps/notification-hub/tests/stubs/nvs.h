#pragma once
#include <stddef.h>
#include <stdint.h>
typedef int nvs_handle_t;
typedef int esp_err_t;
#define ESP_OK 0
#define NVS_READONLY 0
#define NVS_READWRITE 1
int nvs_open(const char *, int, nvs_handle_t *);
void nvs_close(nvs_handle_t);
int nvs_commit(nvs_handle_t);
int nvs_get_str(nvs_handle_t,const char *,char *,size_t *);
int nvs_set_str(nvs_handle_t,const char *,const char *);
int nvs_get_u8(nvs_handle_t,const char *,uint8_t *);
int nvs_get_u16(nvs_handle_t,const char *,uint16_t *);
int nvs_get_u32(nvs_handle_t,const char *,uint32_t *);
int nvs_set_u8(nvs_handle_t,const char *,uint8_t);
int nvs_set_u16(nvs_handle_t,const char *,uint16_t);
int nvs_set_u32(nvs_handle_t,const char *,uint32_t);
