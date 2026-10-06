#include "slot_core.h"
#include <assert.h>

int main(void)
{
    assert(slot_pick_symbol(0) == SLOT_CHERRY);
    assert(slot_pick_symbol(23) == SLOT_CHERRY);
    assert(slot_pick_symbol(24) == SLOT_LEMON);
    assert(slot_pick_symbol(45) == SLOT_LEMON);
    assert(slot_pick_symbol(46) == SLOT_BAR);
    assert(slot_pick_symbol(63) == SLOT_BAR);
    assert(slot_pick_symbol(64) == SLOT_BELL);
    assert(slot_pick_symbol(77) == SLOT_BELL);
    assert(slot_pick_symbol(78) == SLOT_GEM);
    assert(slot_pick_symbol(89) == SLOT_GEM);
    assert(slot_pick_symbol(90) == SLOT_SEVEN);
    assert(slot_pick_symbol(99) == SLOT_SEVEN);

    slot_result_t rare = slot_make_result(99, 99, 99);
    assert(rare.rare_match);
    assert(rare.reels[0] == SLOT_SEVEN);

    slot_result_t ordinary = slot_make_result(0, 24, 46);
    assert(!ordinary.rare_match);
    return 0;
}
