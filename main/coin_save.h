#pragma once
#include "pusher_physics.h"
typedef struct {
    int16_t p[3],v[3],q[4],w[3];
    uint8_t kind,active;
    uint16_t feeder_mask;
} cp_saved_coin_t;
typedef struct {
    uint32_t checksum,format,rng,bank,score,best,drops,collected,lost,prizes,round;
    float phase,game_time;
    uint32_t rod_hits,peg_hits,feeder_exits,exit_bins[3];
    uint32_t events,jackpots,bonus_drops,restocks,capacity_waits,circulation_cycles;
    uint16_t rush_ticks,guard_ticks,event_ticks,bonus_clock,restock_ticks[3],bonus_queue;
    uint8_t collection_mask,event_kind,features,circulation;
    cp_arcade_t arcade;
    cp_saved_coin_t coins[CP_MAX_COINS];
} cp_save_t;
/* Legacy laboratory fixtures only. Roguelike runs are intentionally never restored. */
void cp_save_encode(cp_save_t *save,const cp_game_t *game);
bool cp_save_decode(cp_game_t *game,const cp_save_t *save);
