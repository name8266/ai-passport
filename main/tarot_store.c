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
#define TAROT_SAVE_COALESCE_MS 180

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
static TaskHandle_t s_save_task;
static bool s_ready;
static volatile bool s_error;
/* Keep the roughly 1.4 KiB persistence objects out of app/input/save task
 * stacks. Initialization and save requests are serialized by the application. */
static tarot_store_blob_t s_staging_blob;
static tarot_store_blob_t s_worker_blob;

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

static void make_blob(tarot_store_blob_t *blob, const tarot_persisted_t *data) {
    *blob = (tarot_store_blob_t) {
        .magic = TAROT_STORE_MAGIC,
        .version = TAROT_STORE_VERSION,
        .size = sizeof(tarot_store_blob_t),
        .data = *data,
    };
    blob->crc32 = crc32_bytes(blob, offsetof(tarot_store_blob_t, crc32));
}

static bool persisted_data_valid(const tarot_persisted_t *data) {
    if (!data || data->brightness < 20 || data->brightness > 100 ||
        data->history.count > TAROT_HISTORY_CAPACITY ||
        data->history.next_sequence == 0) {
        return false;
    }
    for (size_t i = 0; i < data->history.count; ++i) {
        const tarot_record_t *record = &data->history.records[i];
        if (record->sequence == 0 || record->spread < 0 ||
            record->spread >= TAROT_SPREAD_COUNT ||
            record->count != tarot_spread_card_count(record->spread) ||
            record->count > TAROT_MAX_DRAW) {
            return false;
        }
        bool seen[TAROT_CARD_COUNT] = { false };
        for (size_t card = 0; card < record->count; ++card) {
            uint8_t id = record->cards[card].card_id;
            if (id >= TAROT_CARD_COUNT || seen[id] || !record->cards[card].revealed) {
                return false;
            }
            seen[id] = true;
        }
    }
    return true;
}

static void save_task(void *argument) {
    (void)argument;
    for (;;) {
        if (xQueueReceive(s_queue, &s_worker_blob, portMAX_DELAY) != pdTRUE) continue;
        vTaskDelay(pdMS_TO_TICKS(TAROT_SAVE_COALESCE_MS));
        while (xQueueReceive(s_queue, &s_worker_blob, 0) == pdTRUE) {}
        esp_err_t error = nvs_set_blob(s_nvs, "state", &s_worker_blob,
                                      sizeof(s_worker_blob));
        if (error == ESP_OK) error = nvs_commit(s_nvs);
        if (error != ESP_OK) {
            s_error = true;
            ESP_LOGE(TAG, "save failed: %s", esp_err_to_name(error));
        }
        else {
            ESP_LOGD(TAG, "save ok; stack remaining=%lu",
                     (unsigned long)uxTaskGetStackHighWaterMark(NULL));
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
    size_t size = sizeof(s_staging_blob);
    error = nvs_get_blob(s_nvs, "state", &s_staging_blob, &size);
    if (error == ESP_OK) {
        bool valid = size == sizeof(s_staging_blob) &&
                     s_staging_blob.magic == TAROT_STORE_MAGIC &&
                     s_staging_blob.version == TAROT_STORE_VERSION &&
                     s_staging_blob.size == sizeof(s_staging_blob) &&
                     s_staging_blob.crc32 == crc32_bytes(
                         &s_staging_blob, offsetof(tarot_store_blob_t, crc32)) &&
                     persisted_data_valid(&s_staging_blob.data);
        if (valid) *data = s_staging_blob.data;
        else ESP_LOGW(TAG, "stored tarot state is invalid; using safe defaults");
    }
    else if (error != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGW(TAG, "stored tarot state read failed; using defaults: %s",
                 esp_err_to_name(error));
    }
    s_queue = xQueueCreate(1, sizeof(tarot_store_blob_t));
    if (!s_queue ||
        xTaskCreate(save_task, "tarot_save", 3072, NULL, 3, &s_save_task) != pdPASS) {
        s_error = true;
        if (s_queue) {
            vQueueDelete(s_queue);
            s_queue = NULL;
        }
        nvs_close(s_nvs);
        s_nvs = 0;
        return false;
    }
    s_error = false;
    s_ready = true;
    return true;
}

void tarot_store_request_save(const tarot_persisted_t *data) {
    if (!s_ready || !data) return;
    make_blob(&s_staging_blob, data);
    if (xQueueOverwrite(s_queue, &s_staging_blob) != pdTRUE) s_error = true;
}

bool tarot_store_has_error(void) {
    return s_error;
}

size_t tarot_store_stack_high_water(void) {
    return s_save_task ? (size_t)uxTaskGetStackHighWaterMark(s_save_task) : 0;
}
