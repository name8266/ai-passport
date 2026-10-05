#include "bsp_audio.h"
#include "stock_sound.h"
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
static bool sleeping, opened;
static unsigned writes, wakes, sleeps, volume, tail_silence, peak, fail_at;
esp_err_t bsp_audio_init(void) { return ESP_OK; }
esp_err_t bsp_audio_wake(void) {
    wakes++;
    sleeping = false;
    return ESP_OK;
}
esp_err_t bsp_audio_set_format(uint32_t hz, uint8_t bits, uint8_t ch) {
    assert(hz == 16000 && bits == 16 && ch == 1);
    if (sleeping)
        return ESP_FAIL;
    opened = true;
    return ESP_OK;
}
void bsp_audio_set_volume(uint8_t v) { volume = v; }
esp_err_t bsp_audio_write(const void *pcm, size_t bytes) {
    assert(opened && !sleeping);
    writes++;
    if (writes == fail_at)
        return ESP_FAIL;
    bool silent = true;
    const int16_t *p = pcm;
    assert(bytes == 640);
    for (unsigned i = 0; i < bytes / 2; i++) {
        unsigned a = (unsigned)abs(p[i]);
        if (a) {
            silent = false;
            if (a > peak)
                peak = a;
        }
    }
    tail_silence = silent ? tail_silence + 1 : 0;
    return ESP_OK;
}
esp_err_t bsp_audio_sleep(void) {
    sleeps++;
    sleeping = true;
    opened = false;
    return ESP_OK;
}
int main(void) {
    assert(stock_sound_play() == ESP_OK);
    assert(wakes == 1 && sleeps == 1 && sleeping);
    assert(volume == 80 && peak == 6000 && tail_silence == 6 && writes == 28);
    writes = peak = tail_silence = 0;
    assert(stock_sound_play() == ESP_OK);
    assert(wakes == 2 && sleeps == 2 && sleeping);
    assert(peak == 6000 && tail_silence == 6 && writes == 28);
    writes = 0;
    fail_at = 4;
    assert(stock_sound_play() == ESP_FAIL);
    assert(writes == 4 && sleeps == 3 && sleeping);
    puts("Stock sound: PASS (first and repeat wake, audible output level, DMA tail, failure "
         "cleanup)");
    return 0;
}
