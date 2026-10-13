#include "zuma_synth.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    zuma_synth_t one, chunks;
    int16_t whole[1024], split[1024];
    zuma_synth_init(&one);
    zuma_synth_init(&chunks);
    zuma_synth_render(&one, whole, 1024);
    for (int i = 0; i < 4; i++)
        zuma_synth_render(&chunks, split + i * 256, 256);
    assert(memcmp(whole, split, sizeof(whole)) == 0);

    int64_t soundtrack_energy = 0;
    for (int i = 0; i < 1024; i++) {
        assert(whole[i] > -12000 && whole[i] < 12000);
        soundtrack_energy += whole[i] < 0 ? -(int32_t)whole[i] : whole[i];
    }
    assert(soundtrack_energy > 100000);

    for (int cue = ZUMA_SOUND_SHOOT; cue <= ZUMA_SOUND_LOSE; cue++) {
        zuma_synth_t dry, wet;
        int16_t baseline[256], effected[256];
        zuma_synth_init(&dry);
        zuma_synth_init(&wet);
        zuma_synth_cue(&wet, (zuma_sound_t)cue);
        zuma_synth_render(&dry, baseline, 256);
        zuma_synth_render(&wet, effected, 256);
        int64_t difference = 0;
        for (int i = 0; i < 256; i++) {
            int32_t delta = effected[i] - baseline[i];
            difference += delta < 0 ? -delta : delta;
        }
        assert(difference > 10000);
    }
    puts("zuma synth: PASS");
    return 0;
}
