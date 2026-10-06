#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    SLOT_CHERRY = 0,
    SLOT_LEMON,
    SLOT_BAR,
    SLOT_BELL,
    SLOT_GEM,
    SLOT_SEVEN,
    SLOT_SYMBOL_COUNT,
} slot_symbol_t;

typedef struct {
    uint8_t reels[3];
    uint32_t multiplier;
    bool jackpot;
} slot_result_t;

uint8_t slot_pick_symbol(uint32_t random_value);
uint32_t slot_multiplier(const uint8_t reels[3]);
uint32_t slot_payout(uint32_t bet, const uint8_t reels[3]);
slot_result_t slot_make_result(uint32_t r0, uint32_t r1, uint32_t r2);
