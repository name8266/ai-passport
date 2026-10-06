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

uint32_t slot_multiplier(const uint8_t reels[3])
{
    const uint8_t a = reels[0], b = reels[1], c = reels[2];
    if (a >= SLOT_SYMBOL_COUNT || b >= SLOT_SYMBOL_COUNT || c >= SLOT_SYMBOL_COUNT) return 0;

    if (a == b && b == c) {
        static const uint8_t triple[SLOT_SYMBOL_COUNT] = {
            10, 6, 8, 15, 25, 50,
        };
        return triple[a];
    }

    unsigned sevens = (a == SLOT_SEVEN) + (b == SLOT_SEVEN) + (c == SLOT_SEVEN);
    if (sevens >= 2) return 3;

    unsigned cherries = (a == SLOT_CHERRY) + (b == SLOT_CHERRY) + (c == SLOT_CHERRY);
    if (cherries >= 2) return 2;

    return 0;
}

uint32_t slot_payout(uint32_t bet, const uint8_t reels[3])
{
    uint32_t multiplier = slot_multiplier(reels);
    if (multiplier == 0 || bet == 0) return 0;
    if (bet > UINT32_MAX / multiplier) return UINT32_MAX;
    return bet * multiplier;
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
    result.multiplier = slot_multiplier(result.reels);
    result.jackpot = result.reels[0] == SLOT_SEVEN &&
                     result.reels[1] == SLOT_SEVEN &&
                     result.reels[2] == SLOT_SEVEN;
    return result;
}
