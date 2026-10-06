#include "multi_slot_core.h"
#include <string.h>

static const uint16_t PAYOUT[SLOT_SYMBOL_COUNT] = {20, 25, 35, 45, 60, 100};

bool multi_slot_line_cells(uint8_t line, uint8_t cells[3])
{
    if (!cells || line >= MULTI_SLOT_LINES) return false;
    uint8_t start, rows[3];
    if (line < 9) {
        start = line % 3;
        rows[0] = rows[1] = rows[2] = line / 3;
    } else if (line < 15) {
        start = (line - 9) % 3;
        bool down = line < 12;
        rows[0] = down ? 0 : 2;
        rows[1] = 1;
        rows[2] = down ? 2 : 0;
    } else {
        static const uint8_t triangular[4][3] = {
            {0,1,0}, {1,0,1}, {1,2,1}, {2,1,2},
        };
        start = (line - 15) % 3;
        memcpy(rows, triangular[(line - 15) / 3], sizeof(rows));
    }
    for (uint8_t i = 0; i < 3; i++)
        cells[i] = rows[i] * MULTI_SLOT_COLUMNS + start + i;
    return true;
}

multi_slot_result_t multi_slot_evaluate(const uint8_t cells[MULTI_SLOT_CELLS])
{
    multi_slot_result_t result = {0};
    if (!cells) return result;
    memcpy(result.cells, cells, sizeof(result.cells));
    for (uint8_t i = 0; i < MULTI_SLOT_CELLS; i++)
        if (cells[i] >= SLOT_SYMBOL_COUNT) return result;
    for (uint8_t line = 0; line < MULTI_SLOT_LINES; line++) {
        uint8_t indices[3];
        multi_slot_line_cells(line, indices);
        uint8_t symbol = cells[indices[0]];
        if (symbol != cells[indices[1]] || symbol != cells[indices[2]]) continue;
        result.line_mask |= (uint32_t)1 << line;
        result.line_count++;
        result.score += PAYOUT[symbol] * (line < 9 ? 1u : 2u);
        for (uint8_t i = 0; i < 3; i++) result.cell_mask |= (uint16_t)1 << indices[i];
    }
    /* Small consolation: matching leftmost pair, only when no triple won.
       No reroll or forced win is used; the fifteen results stay independent. */
    if (!result.line_count) {
        for (uint8_t row = 0; row < MULTI_SLOT_ROWS; row++) {
            uint8_t a = row * MULTI_SLOT_COLUMNS;
            if (cells[a] != cells[a + 1]) continue;
            result.pair_count++;
            result.score += 5;
            result.cell_mask |= (uint16_t)3 << a;
        }
    }
    return result;
}

static uint32_t mix32(uint32_t value)
{
    value ^= value >> 16;
    value *= 0x7feb352du;
    value ^= value >> 15;
    value *= 0x846ca68bu;
    return value ^ (value >> 16);
}

multi_slot_plan_t multi_slot_prepare_strip(uint8_t strip[MULTI_SLOT_STRIP_LENGTH],
    uint32_t phase_q8, uint8_t column, const uint8_t result[3], uint32_t seed)
{
    multi_slot_plan_t plan={phase_q8%(MULTI_SLOT_STRIP_LENGTH*256u),0};
    plan.end_q8=plan.start_q8;
    if(!strip||!result||column>=MULTI_SLOT_COLUMNS)return plan;
    for(int i=0;i<3;i++)if(result[i]>=SLOT_SYMBOL_COUNT)return plan;
    uint32_t start=plan.start_q8/256u;
    /* Keep the five currently laid-out cells unchanged to avoid a start flash. */
    for(uint32_t i=0;i<MULTI_SLOT_STRIP_LENGTH;i++) {
        uint32_t relative=(i+MULTI_SLOT_STRIP_LENGTH-start)%MULTI_SLOT_STRIP_LENGTH;
        if(relative<=2||relative>=MULTI_SLOT_STRIP_LENGTH-2)continue;
        seed=mix32(seed+0x9e3779b9u);strip[i]=slot_pick_symbol(seed);
    }
    seed=mix32(seed+0x9e3779b9u);
    uint32_t end=start+MULTI_SLOT_STRIP_LENGTH*(3u+column)+5u+seed%17u;
    for(uint32_t row=0;row<3;row++)strip[(end-row)%MULTI_SLOT_STRIP_LENGTH]=result[row];
    plan.end_q8=end*256u;
    return plan;
}

multi_slot_result_t multi_slot_make_result(uint32_t seed)
{
    uint8_t cells[MULTI_SLOT_CELLS];
    for (uint8_t i = 0; i < MULTI_SLOT_CELLS; i++) {
        seed = mix32(seed + 0x9e3779b9u);
        cells[i] = slot_pick_symbol(seed);
    }
    return multi_slot_evaluate(cells);
}

uint32_t slot_classic_score(const uint8_t reels[3])
{
    if (!reels) return 0;
    for (uint8_t i = 0; i < 3; i++) if (reels[i] >= SLOT_SYMBOL_COUNT) return 0;
    if (reels[0] == reels[1] && reels[1] == reels[2]) return PAYOUT[reels[0]];
    if (reels[0] == reels[1] || reels[1] == reels[2] || reels[0] == reels[2]) return 5;
    return 0;
}
