#include "slot_core.h"
#include <assert.h>
#include <stdint.h>

int main(void)
{
    uint8_t reels[3];

    reels[0] = reels[1] = reels[2] = SLOT_SEVEN;
    assert(slot_multiplier(reels) == 50);
    assert(slot_payout(10, reels) == 500);

    reels[0] = reels[1] = reels[2] = SLOT_GEM;
    assert(slot_multiplier(reels) == 25);

    reels[0] = SLOT_SEVEN; reels[1] = SLOT_SEVEN; reels[2] = SLOT_LEMON;
    assert(slot_multiplier(reels) == 3);

    reels[0] = SLOT_CHERRY; reels[1] = SLOT_BAR; reels[2] = SLOT_CHERRY;
    assert(slot_multiplier(reels) == 2);

    reels[0] = SLOT_CHERRY; reels[1] = SLOT_LEMON; reels[2] = SLOT_BAR;
    assert(slot_multiplier(reels) == 0);

    assert(slot_pick_symbol(0) == SLOT_CHERRY);
    assert(slot_pick_symbol(23) == SLOT_CHERRY);
    assert(slot_pick_symbol(24) == SLOT_LEMON);
    assert(slot_pick_symbol(45) == SLOT_LEMON);
    assert(slot_pick_symbol(46) == SLOT_BAR);
    assert(slot_pick_symbol(99) == SLOT_SEVEN);

    slot_result_t result = slot_make_result(99, 99, 99);
    assert(result.jackpot);
    assert(result.multiplier == 50);

    return 0;
}
