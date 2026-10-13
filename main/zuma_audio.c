#include "zuma_audio.h"
#include "zuma_synth.h"
#include "bsp_audio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include <stdint.h>

#define CHUNK 256
static QueueHandle_t s_cues;
static const char *TAG = "star_music";

static void audio_task(void *arg) {
    (void)arg;
    if (bsp_audio_init() != ESP_OK ||
        bsp_audio_set_format(ZUMA_SYNTH_RATE, 16, 1) != ESP_OK) {
        ESP_LOGE(TAG, "Audio codec unavailable");
        vTaskDelete(NULL);
        return;
    }
    bsp_audio_set_volume(42);
    zuma_synth_t synth;
    zuma_synth_init(&synth);
    int16_t pcm[CHUNK];
    for (;;) {
        zuma_sound_t newest;
        while (xQueueReceive(s_cues, &newest, 0) == pdTRUE)
            zuma_synth_cue(&synth, newest);
        zuma_synth_render(&synth, pcm, CHUNK);
        if (bsp_audio_write(pcm, sizeof(pcm)) != ESP_OK) {
            ESP_LOGE(TAG, "PCM output stopped");
            break;
        }
    }
    vTaskDelete(NULL);
}
bool zuma_audio_start(void) {
    s_cues = xQueueCreate(8, sizeof(zuma_sound_t));
    if (!s_cues) return false;
    if (xTaskCreate(audio_task, "star_music", 3072, NULL, 6, NULL) != pdPASS) {
        vQueueDelete(s_cues);
        s_cues = NULL;
        return false;
    }
    return true;
}
void zuma_audio_cue(zuma_sound_t sound) {
    if (s_cues) (void)xQueueSend(s_cues, &sound, 0);
}
