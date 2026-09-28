#include "tarot_store.h"

#include <stddef.h>
#include <string.h>

#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#define TAROT_STORE_MAGIC 0x54415254U
#define TAROT_STORE_VERSION 1U

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t size;
    tarot_persisted_t data;
    uint32_t crc32;
} tarot_store_blob_t;

static const char *TAG = "tarot_store";
static QueueHandle_t s_queue;
static nvs_handle_t s_nvs;
static bool s_ready;
static volatile bool s_error;

static uint32_t crc32_bytes(const void *data, size_t length) {
    const uint8_t *bytes = data;
    uint32_t crc = 0xFFFFFFFFU;
    for (size_t i = 0; i < length; ++i) {
        crc ^= bytes[i];
        for (int bit = 0; bit < 8; ++bit) {
            crc = (crc >> 1) ^ (0xEDB88320U & (uint32_t)-(int32_t)(crc & 1U));
        }
    }
    return ~crc;
}

static void defaults(tarot_persisted_t *data) {
    memset(data, 0, sizeof(*data));
    data->reversals_enabled = true;
    data->sound_enabled = true;
    data->brightness = 80;
    tarot_history_init(&data->history);
}

static tarot_store_blob_t make_blob(const tarot_persisted_t *data) {
    tarot_store_blob_t blob = {
        .magic = TAROT_STORE_MAGIC,
        .version = TAROT_STORE_VERSION,
        .size = sizeof(tarot_store_blob_t),
        .data = *data,
    };
    blob.crc32 = crc32_bytes(&blob, offsetof(tarot_store_blob_t, crc32));
    return blob;
}

static void save_task(void *argument) {
    (void)argument;
    tarot_store_blob_t blob;
    for (;;) {
        if (xQueueReceive(s_queue, &blob, portMAX_DELAY) != pdTRUE) continue;
        esp_err_t error = nvs_set_blob(s_nvs, "state", &blob, sizeof(blob));
        if (error == ESP_OK) error = nvs_commit(s_nvs);
        if (error != ESP_OK) {
            s_error = true;
            ESP_LOGE(TAG, "save failed: %s", esp_err_to_name(error));
        }
    }
}

bool tarot_store_init(tarot_persisted_t *data) {
    if (!data) return false;
    defaults(data);
    if (s_ready) return true;
    esp_err_t error = nvs_flash_init();
    if (error != ESP_OK) {
        s_error = true;
        ESP_LOGW(TAG, "NVS unavailable; continuing without persistence: %s", esp_err_to_name(error));
        return false;
    }
    error = nvs_open("tarot", NVS_READWRITE, &s_nvs);
    if (error != ESP_OK) {
        s_error = true;
        return false;
    }
    tarot_store_blob_t blob;
    size_t size = sizeof(blob);
    error = nvs_get_blob(s_nvs, "state", &blob, &size);
    if (error == ESP_OK && size == sizeof(blob) &&
        blob.magic == TAROT_STORE_MAGIC && blob.version == TAROT_STORE_VERSION &&
        blob.size == sizeof(blob) &&
        blob.crc32 == crc32_bytes(&blob, offsetof(tarot_store_blob_t, crc32))) {
        *data = blob.data;
        if (data->brightness < 20 || data->brightness > 100 ||
            data->history.count > TAROT_HISTORY_CAPACITY) {
            defaults(data);
        }
    }
    s_queue = xQueueCreate(1, sizeof(tarot_store_blob_t));
    if (!s_queue || xTaskCreate(save_task, "tarot_save", 3072, NULL, 3, NULL) != pdPASS) {
        s_error = true;
        return false;
    }
    s_ready = true;
    return true;
}

void tarot_store_request_save(const tarot_persisted_t *data) {
    if (!s_ready || !data) return;
    tarot_store_blob_t blob = make_blob(data);
    if (xQueueOverwrite(s_queue, &blob) != pdTRUE) s_error = true;
}

bool tarot_store_has_error(void) {
    return s_error;
}
