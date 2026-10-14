#pragma once
#include "pusher_physics.h"
#include <stddef.h>
#define CP_SAVE_META_BYTES (offsetof(cp_game_t,balls)-offsetof(cp_game_t,rng))
typedef struct { int16_t x,y,z,vx,vy,vz; uint8_t kind,flags; } cp_saved_coin_t;
typedef struct { int16_t x,y,vx,vy; uint8_t kind,active; } cp_saved_ball_t;
typedef struct {
    uint32_t checksum;
    uint8_t meta[CP_SAVE_META_BYTES];
    cp_saved_coin_t coins[CP_MAX_COINS];
    cp_saved_ball_t balls[CP_MAX_BALLS];
    uint32_t slot_hits[5],peg_hits;
} cp_save_t;
void cp_save_encode(cp_save_t *save,const cp_game_t *game);
bool cp_save_decode(cp_game_t *game,const cp_save_t *save);
