#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "tarot_history.h"

typedef struct {
    bool reversals_enabled;
    bool sound_enabled;
    uint8_t brightness;
    tarot_history_t history;
} tarot_persisted_t;

bool tarot_store_init(tarot_persisted_t *data);
void tarot_store_request_save(const tarot_persisted_t *data);
bool tarot_store_has_error(void);
size_t tarot_store_stack_high_water(void);
