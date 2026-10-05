#include "stock_sound.h"
#include "bsp_audio.h"
#include <stdint.h>
#include <string.h>
#define SAMPLES 320
#define TONE_BLOCKS 20
static esp_err_t silence(unsigned blocks) {
    int16_t pcm[SAMPLES] = {0};
    for (unsigned i = 0; i < blocks; i++) {
        esp_err_t e = bsp_audio_write(pcm, sizeof(pcm));
        if (e != ESP_OK)
            return e;
    }
    return ESP_OK;
}
esp_err_t stock_sound_play(void) {
    esp_err_t e = bsp_audio_init();
    if (e != ESP_OK)
        return e;
    e = bsp_audio_wake();
    if (e != ESP_OK)
        return e;
    e = bsp_audio_set_format(16000, 16, 1);
    if (e != ESP_OK) {
        bsp_audio_sleep();
        return e;
    }
    /* Same output level and peak as the board's audio demonstration. */
    bsp_audio_set_volume(80);
    e = silence(2);
    int16_t pcm[SAMPLES];
    for (unsigned block = 0; e == ESP_OK && block < TONE_BLOCKS; block++) {
        for (unsigned i = 0; i < SAMPLES; i++) {
            unsigned pos = block * SAMPLES + i;
            int amplitude = 6000;
            if (pos < 160)
                amplitude = (int)(6000 * pos / 160);
            if (pos >= SAMPLES * TONE_BLOCKS - 1280)
                amplitude = (int)(6000 * (SAMPLES * TONE_BLOCKS - pos) / 1280);
            pcm[i] = (int16_t)((pos / 9) % 2 ? amplitude : -amplitude);
        }
        e = bsp_audio_write(pcm, sizeof(pcm));
    }
    /* I2S writes queue DMA data. 120ms of silence lets the tone leave the
       six 240-frame DMA buffers before shutting down the codec. */
    if (e == ESP_OK)
        e = silence(6);
    esp_err_t stopped = bsp_audio_sleep();
    return e != ESP_OK ? e : stopped;
}
