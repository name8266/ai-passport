#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define TAROT_CARD_COUNT 78
#define TAROT_MAX_DRAW 10

typedef enum {
    TAROT_SPREAD_DAILY = 0,
    TAROT_SPREAD_SINGLE,
    TAROT_SPREAD_THREE,
    TAROT_SPREAD_CELTIC_CROSS,
    TAROT_SPREAD_COUNT,
} tarot_spread_t;

typedef struct {
    uint8_t card_id;
    bool reversed;
    bool revealed;
} tarot_draw_t;

typedef struct {
    tarot_spread_t spread;
    uint8_t count;
    uint8_t selected;
    tarot_draw_t cards[TAROT_MAX_DRAW];
} tarot_session_t;

typedef uint32_t (*tarot_random_fn_t)(void);

size_t tarot_spread_card_count(tarot_spread_t spread);
const char *tarot_spread_name(tarot_spread_t spread);
const char *tarot_position_name(tarot_spread_t spread, size_t index);
bool tarot_session_start(tarot_session_t *session, tarot_spread_t spread,
                         bool reversals_enabled, tarot_random_fn_t random_fn);
bool tarot_session_move(tarot_session_t *session, int delta);
bool tarot_session_reveal(tarot_session_t *session);
bool tarot_session_all_revealed(const tarot_session_t *session);
