#include "tarot_audio.h"

#include <stdint.h>

#include "bsp_audio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#define AUDIO_RATE 16000
#define AUDIO_CHUNK 128
#define AUDIO_QUEUE_DEPTH 6

typedef enum {
    AUDIO_REVEAL = 1,
    AUDIO_CONFIRM,
    AUDIO_POWER_CHANGED,
} audio_event_t;

static const char *TAG = "tarot_audio";
static QueueHandle_t s_queue;
static TaskHandle_t s_task;
static volatile bool s_enabled = true;
static volatile bool s_sleep_requested;
static bool s_sleeping;

static void play_note(unsigned frequency, unsigned duration_ms, int amplitude) {
    int16_t samples[AUDIO_CHUNK];
    unsigned remaining = AUDIO_RATE * duration_ms / 1000;
    unsigned phase = 0;
    unsigned period = AUDIO_RATE / frequency;
    while (remaining) {
        unsigned count = remaining < AUDIO_CHUNK ? remaining : AUDIO_CHUNK;
        for (unsigned i = 0; i < count; ++i) {
            int value = phase < period / 2 ? amplitude : -amplitude;
            if (remaining < AUDIO_RATE / 100) {
                value = value * (int)remaining / (AUDIO_RATE / 100);
            }
            samples[i] = (int16_t)value;
            phase = (phase + 1) % period;
        }
        if (bsp_audio_write(samples, count * sizeof(samples[0])) != ESP_OK) return;
        remaining -= count;
    }
}

static bool wake_if_needed(void) {
    if (!s_sleeping) return true;
    if (bsp_audio_wake() != ESP_OK) {
        ESP_LOGE(TAG, "audio wake failed");
        return false;
    }
    s_sleeping = false;
    return true;
}

static void audio_task(void *argument) {
    (void)argument;
    audio_event_t event;
    for (;;) {
        if (xQueueReceive(s_queue, &event, portMAX_DELAY) != pdTRUE) continue;
        if (event == AUDIO_POWER_CHANGED) {
            bool should_sleep = s_sleep_requested;
            if (should_sleep && !s_sleeping) {
                if (bsp_audio_sleep() == ESP_OK) s_sleeping = true;
                else ESP_LOGE(TAG, "audio suspend failed");
            }
            else if (!should_sleep) {
                (void)wake_if_needed();
            }
            continue;
        }
        if (!s_enabled || !wake_if_needed()) continue;
        if (bsp_audio_set_format(AUDIO_RATE, 16, 1) != ESP_OK) continue;
        bsp_audio_set_volume(42);
        if (event == AUDIO_REVEAL) {
            play_note(440, 55, 2800);
            play_note(660, 85, 2400);
        }
        else if (event == AUDIO_CONFIRM) {
            play_note(740, 55, 2200);
        }
    }
}

bool tarot_audio_init(void) {
    if (s_task) return true;
    if (bsp_audio_init() != ESP_OK) return false;
    if (!s_queue) s_queue = xQueueCreate(AUDIO_QUEUE_DEPTH, sizeof(audio_event_t));
    if (!s_queue) return false;
    if (xTaskCreate(audio_task, "tarot_audio", 3072, NULL, 4, &s_task) != pdPASS) {
        vQueueDelete(s_queue);
        s_queue = NULL;
        return false;
    }
    return true;
}

void tarot_audio_set_enabled(bool enabled) {
    s_enabled = enabled;
}

static void enqueue_effect(audio_event_t event) {
    if (s_queue && xQueueSend(s_queue, &event, 0) != pdTRUE) {
        ESP_LOGW(TAG, "audio effect queue full; event=%d", event);
    }
}

/* Power-state commands must not be dropped behind a burst of key sounds. Every
 * queued control event reads s_sleep_requested, so even suspend/resume changes
 * that cross in the queue converge on the latest requested state. */
static void enqueue_control(bool should_sleep) {
    if (!s_queue) return;
    s_sleep_requested = should_sleep;
    audio_event_t event = AUDIO_POWER_CHANGED;
    if (xQueueSendToFront(s_queue, &event, 0) == pdTRUE) return;
    xQueueReset(s_queue);
    if (xQueueSend(s_queue, &event, 0) != pdTRUE) {
        ESP_LOGE(TAG, "audio control queue failed; event=%d", event);
    }
}

void tarot_audio_play_reveal(void) {
    if (s_enabled) enqueue_effect(AUDIO_REVEAL);
}

void tarot_audio_play_confirm(void) {
    if (s_enabled) enqueue_effect(AUDIO_CONFIRM);
}

void tarot_audio_suspend(void) {
    enqueue_control(true);
}

void tarot_audio_resume(void) {
    enqueue_control(false);
}

size_t tarot_audio_stack_high_water(void) {
    return s_task ? (size_t)uxTaskGetStackHighWaterMark(s_task) : 0;
}
