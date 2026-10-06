#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "slot_core.h"

#define MULTI_SLOT_COLUMNS 5
#define MULTI_SLOT_ROWS 3
#define MULTI_SLOT_CELLS 15
#define MULTI_SLOT_LINES 27
#define MULTI_SLOT_STRIP_LENGTH 24

typedef struct {
    uint8_t cells[MULTI_SLOT_CELLS]; /* row-major */
    uint32_t score;
    uint32_t line_mask;
    uint16_t cell_mask;
    uint8_t line_count;
    uint8_t pair_count;
} multi_slot_result_t;

/* Nine horizontal, six diagonal, and twelve V/inverted-V triples. */
bool multi_slot_line_cells(uint8_t line, uint8_t cells[3]);
multi_slot_result_t multi_slot_evaluate(const uint8_t cells[MULTI_SLOT_CELLS]);
multi_slot_result_t multi_slot_make_result(uint32_t seed);
typedef struct { uint32_t start_q8, end_q8; } multi_slot_plan_t;
/* Preserve the visible symbols while preparing an off-screen stopping group. */
multi_slot_plan_t multi_slot_prepare_strip(uint8_t strip[MULTI_SLOT_STRIP_LENGTH],
    uint32_t phase_q8, uint8_t column, const uint8_t result[3], uint32_t seed);
uint32_t slot_classic_score(const uint8_t reels[3]);
