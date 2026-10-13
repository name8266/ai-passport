#include "zuma_synth.h"
#include <string.h>

/* Integer phase synthesis keeps the 16 kHz audio worker light on ESP32-C3. */
static int32_t triangle(uint32_t *phase, uint16_t hz) {
    *phase += (uint32_t)hz * 268435u; /* approximately 2^32 / 16000 */
    uint32_t p = *phase >> 16;
    return p < 32768u ? (int32_t)p * 2 - 32768 : 98304 - (int32_t)p * 2;
}

static int32_t noise(zuma_synth_t *s) {
    uint32_t x = s->noise;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    s->noise = x;
    return (int32_t)(x >> 16) - 32768;
}

void zuma_synth_init(zuma_synth_t *s) {
    memset(s, 0, sizeof(*s));
    s->noise = 0x6d2b79f5u;
}

void zuma_synth_cue(zuma_synth_t *s, zuma_sound_t cue) {
    if (cue == ZUMA_SOUND_NONE) return;
    s->cue = cue;
    s->cue_total = (cue == ZUMA_SOUND_WIN || cue == ZUMA_SOUND_LOSE) ? 9000 : 3000;
    s->cue_left = s->cue_total;
    s->cue_phase = 0;
}

static uint16_t cue_frequency(const zuma_synth_t *s) {
    int elapsed = s->cue_total - s->cue_left;
    switch (s->cue) {
    case ZUMA_SOUND_SHOOT: return (uint16_t)(740 - elapsed * 290 / s->cue_total);
    case ZUMA_SOUND_HIT: return 185;
    case ZUMA_SOUND_MATCH: return (uint16_t)(570 + elapsed * 390 / s->cue_total);
    case ZUMA_SOUND_COMBO: return (uint16_t)(680 + elapsed * 620 / s->cue_total);
    case ZUMA_SOUND_SWAP: return elapsed < s->cue_total / 2 ? 392 : 587;
    case ZUMA_SOUND_WIN: return (uint16_t)(330 + (elapsed / 2250) * 110);
    case ZUMA_SOUND_LOSE: return (uint16_t)(392 - elapsed * 245 / s->cue_total);
    default: return 0;
    }
}

void zuma_synth_render(zuma_synth_t *s, int16_t *out, size_t count) {
    /* A minor pentatonic phrase; soft accents leave room for event cues. */
    static const uint16_t melody[16] = {
        392, 0, 440, 523, 0, 440, 392, 330,
        294, 0, 330, 392, 0, 330, 294, 262
    };
    static const uint16_t bass[4] = {110, 98, 131, 110};
    for (size_t i = 0; i < count; i++, s->frame++) {
        int beat = (s->frame / 8000u) % 16u;
        int within = s->frame % 8000u;
        int32_t sample = 0;
        if (melody[beat] && within < 6800) {
            int32_t attack = within < 180 ? within * 2048 / 180 : 2048;
            int32_t envelope = attack * (6800 - within) / 6800;
            sample += triangle(&s->melody_phase, melody[beat]) * envelope / 32768;
            sample += triangle(&s->shimmer_phase, melody[beat] * 2) * envelope / 196608;
        }
        sample += triangle(&s->bass_phase, bass[beat / 4]) * 480 / 32768;
        if (within < 2100) {
            int32_t envelope = (2100 - within);
            int32_t strength = beat % 4 == 0 ? 1350 : 650;
            int32_t drum = triangle(&s->drum_phase, 55) * strength / 32768;
            sample += drum * envelope / 2100;
            if (within < 650) {
                int32_t tap = noise(s) * (beat % 4 == 0 ? 540 : 250) / 32768;
                sample += tap * (650 - within) / 650;
            }
        }
        if (within >= 4000 && within < 4700) {
            int32_t shaker = noise(s) * 290 / 32768;
            sample += shaker * (4700 - within) / 700;
        }
        if (s->cue_left > 0) {
            uint16_t hz = cue_frequency(s);
            int32_t envelope = s->cue_left * 4800 / s->cue_total;
            sample += triangle(&s->cue_phase, hz) * envelope / 32768;
            if (s->cue_left > s->cue_total - 240 &&
                (s->cue == ZUMA_SOUND_HIT || s->cue == ZUMA_SOUND_MATCH || s->cue == ZUMA_SOUND_COMBO)) {
                int32_t sparkle = noise(s) * 700 / 32768;
                sample += sparkle * (s->cue_left - (s->cue_total - 240)) / 240;
            }
            s->cue_left--;
        }
        if (sample > 12000) sample = 12000;
        if (sample < -12000) sample = -12000;
        out[i] = (int16_t)sample;
    }
}
