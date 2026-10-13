#pragma once

#include "zuma_game.h"
#include <stddef.h>
#include <stdint.h>

#define ZUMA_SYNTH_RATE 16000

typedef struct {
    uint32_t frame;
    uint32_t melody_phase;
    uint32_t shimmer_phase;
    uint32_t bass_phase;
    uint32_t drum_phase;
    uint32_t cue_phase;
    uint32_t noise;
    zuma_sound_t cue;
    int cue_left;
    int cue_total;
} zuma_synth_t;

void zuma_synth_init(zuma_synth_t *s);
void zuma_synth_cue(zuma_synth_t *s, zuma_sound_t cue);
void zuma_synth_render(zuma_synth_t *s, int16_t *out, size_t count);
