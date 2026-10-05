#pragma once
#include <stdint.h>
typedef int esp_err_t;
typedef struct esp_netif_t esp_netif_t;
typedef void *QueueHandle_t;
typedef enum {BSP_BTN_UP,BSP_BTN_DOWN,BSP_BTN_OK} bsp_btn_t;
typedef enum {BSP_BTN_PRESS,BSP_BTN_CLICK,BSP_BTN_DOUBLE,BSP_BTN_LONG} bsp_btn_ev_t;
#define ESP_OK 0
#define ESP_FAIL -1
typedef struct {uint32_t addr;} esp_ip4_addr_t;
typedef struct {esp_ip4_addr_t ip,netmask,gw;} esp_netif_ip_info_t;
esp_err_t esp_netif_get_ip_info(esp_netif_t *netif,esp_netif_ip_info_t *out);
