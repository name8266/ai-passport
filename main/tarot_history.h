#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "tarot_model.h"

#define TAROT_HISTORY_CAPACITY 32

typedef struct {
    uint32_t sequence;
    tarot_spread_t spread;
    uint8_t count;
    bool favorite;
    tarot_draw_t cards[TAROT_MAX_DRAW];
} tarot_record_t;

typedef struct {
    uint32_t next_sequence;
    uint8_t count;
    tarot_record_t records[TAROT_HISTORY_CAPACITY];
} tarot_history_t;

void tarot_history_init(tarot_history_t *history);
bool tarot_history_add(tarot_history_t *history, const tarot_session_t *session);
const tarot_record_t *tarot_history_recent(const tarot_history_t *history, size_t newest_index);
bool tarot_history_toggle_favorite(tarot_history_t *history, uint32_t sequence);
void tarot_history_clear_nonfavorites(tarot_history_t *history);
bool tarot_history_full_with_favorites(const tarot_history_t *history);
