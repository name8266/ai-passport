#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"
/* Single iPhone ANCS consumer, ESP-IDF 5.5.3 NimBLE host.
 * Events remain owned by the existing archive/decoder application.
 */
typedef struct {
    void (*link)(bool connected,bool paired,bool ready,uint32_t pairing_code);
    void (*source)(const uint8_t *bytes,uint16_t length);
    void (*data)(const uint8_t *bytes,uint16_t length);
    void (*write_error)(void);
} hub_ble_callbacks_t;
esp_err_t hub_ble_nimble_start(const hub_ble_callbacks_t *callbacks);
bool hub_ble_nimble_request_details(const uint8_t *bytes,size_t length);
void hub_ble_nimble_disconnect(void);
