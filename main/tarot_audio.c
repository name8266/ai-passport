#include "tarot_audio.h"

#include <stdint.h>

#include "bsp_audio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#define AUDIO_RATE 16000
#define AUDIO_CHUNK 128

typedef enum { AUDIO_REVEAL = 1, AUDIO_CONFIRM = 2 } audio_event_t;

static QueueHandle_t s_queue;
static bool s_enabled = true;

static void play_note(unsigned frequency, unsigned milliseconds, int amplitude) {
    int16_t samples[AUDIO_CHUNK];
    unsigned total = AUDIO_RATE * milliseconds / 1000;
    unsigned phase = 0;
    unsigned period = AUDIO_RATE / frequency;
    while (total > 0) {
        unsigned count = total < AUDIO_CHUNK ? total : AUDIO_CHUNK;
        for (unsigned i = 0; i < count; ++i) {
            int value = phase < period / 2 ? amplitude : -amplitude;
            if (total < AUDIO_RATE / 100) value = value * (int)total / (AUDIO_RATE / 100);
            samples[i] = (int16_t)value;
            phase = (phase + 1) % period;
        }
        if (bsp_audio_write(samples, count * sizeof(samples[0])) != ESP_OK) return;
        total -= count;
    }
}

static void audio_task(void *argument) {
    (void)argument;
    audio_event_t event;
    for (;;) {
        if (xQueueReceive(s_queue, &event, portMAX_DELAY) != pdTRUE || !s_enabled) continue;
        if (bsp_audio_set_format(AUDIO_RATE, 16, 1) != ESP_OK) continue;
        bsp_audio_set_volume(42);
        if (event == AUDIO_REVEAL) {
            play_note(440, 55, 2800);
            play_note(660, 85, 2400);
        } else {
            play_note(740, 55, 2200);
        }
    }
}

bool tarot_audio_init(void) {
    if (s_queue) return true;
    if (bsp_audio_init() != ESP_OK) return false;
    s_queue = xQueueCreate(4, sizeof(audio_event_t));
    return s_queue && xTaskCreate(audio_task, "tarot_audio", 3072, NULL, 4, NULL) == pdPASS;
}

void tarot_audio_set_enabled(bool enabled) { s_enabled = enabled; }

static void enqueue(audio_event_t event) {
    if (s_enabled && s_queue) (void)xQueueSend(s_queue, &event, 0);
}

void tarot_audio_play_reveal(void) { enqueue(AUDIO_REVEAL); }
void tarot_audio_play_confirm(void) { enqueue(AUDIO_CONFIRM); }
