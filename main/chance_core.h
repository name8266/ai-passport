#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    CHANCE_RED = 0,
    CHANCE_BLACK,
    CHANCE_GREEN,
} chance_roulette_color_t;

typedef struct {
    uint8_t number;
    chance_roulette_color_t color;
} chance_roulette_result_t;

uint8_t chance_die(uint32_t random_value);
chance_roulette_result_t chance_roulette(uint32_t random_value);
bool chance_is_triple(const uint8_t reels[3]);
bool chance_is_pair(const uint8_t reels[3]);
