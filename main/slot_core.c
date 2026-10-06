#include "slot_core.h"

static const uint8_t k_weights[SLOT_SYMBOL_COUNT] = {
    24, 22, 18, 14, 12, 10,
};

uint8_t slot_pick_symbol(uint32_t random_value)
{
    uint32_t pick = random_value % 100u;
    uint32_t cumulative = 0;
    for (uint8_t i = 0; i < SLOT_SYMBOL_COUNT; ++i) {
        cumulative += k_weights[i];
        if (pick < cumulative) return i;
    }
    return SLOT_SEVEN;
}

slot_result_t slot_make_result(uint32_t r0, uint32_t r1, uint32_t r2)
{
    slot_result_t result = {
        .reels = {
            slot_pick_symbol(r0),
            slot_pick_symbol(r1),
            slot_pick_symbol(r2),
        },
    };
    result.rare_match = result.reels[0] == SLOT_SEVEN &&
                        result.reels[1] == SLOT_SEVEN &&
                        result.reels[2] == SLOT_SEVEN;
    return result;
}
