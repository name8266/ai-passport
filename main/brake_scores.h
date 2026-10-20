#pragma once
#include "brake_hero.h"
#include <stddef.h>
#define BH_TOP 10
#define BH_SCORE_BYTES (16 + BH_TOP * 32)
typedef struct {
    uint32_t peak_mms, remaining_mm, max_pedal_n, sequence;
    char name[8];
    uint8_t tent_used, obstacle;
} bh_record_t;
typedef struct { uint32_t next_sequence; uint8_t count; bh_record_t rows[BH_TOP]; } bh_scores_t;
void bh_scores_clear(bh_scores_t *t);
int bh_scores_add(bh_scores_t *t, const bh_state_t *s);
void bh_scores_encode(const bh_scores_t *t, uint8_t data[BH_SCORE_BYTES]);
bool bh_scores_decode(bh_scores_t *t, const uint8_t *data, size_t size);
