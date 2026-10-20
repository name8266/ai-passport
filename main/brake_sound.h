#pragma once
#include <stdbool.h>
#include <stdint.h>
typedef enum { SND_START, SND_100, SND_TENT, SND_WARN, SND_BREAK, SND_NEAR,
    SND_WIN, SND_LOSE, SND_WALL, SND_CLIFF, SND_TRUCK, SND_CONSTRUCTION,
    SND_COUNT } bh_sound_t;
typedef struct { const uint8_t *samples; uint32_t count; } bh_clip_t;
extern const bh_clip_t bh_clips[SND_COUNT];
/* Bounded nonblocking producer. Worker alone owns the BSP codec. */
bool bh_sound_init(void);
void bh_sound_post(bh_sound_t sound, bool urgent);
void bh_sound_new_round(void);
uint32_t bh_sound_max_gap_us(void);
