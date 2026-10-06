#include "chance_core.h"
#include <assert.h>

int main(void)
{
    assert(chance_die(0) == 1);
    assert(chance_die(5) == 6);
    assert(chance_die(6) == 1);

    chance_roulette_result_t zero = chance_roulette(0);
    assert(zero.number == 0 && zero.color == CHANCE_GREEN);

    chance_roulette_result_t red = chance_roulette(1);
    assert(red.number == 1 && red.color == CHANCE_RED);

    chance_roulette_result_t black = chance_roulette(2);
    assert(black.number == 2 && black.color == CHANCE_BLACK);

    uint8_t reels[3] = {1, 1, 1};
    assert(chance_is_triple(reels));
    assert(chance_is_pair(reels));
    reels[2] = 2;
    assert(!chance_is_triple(reels));
    assert(chance_is_pair(reels));
    reels[1] = 3;
    assert(!chance_is_pair(reels));
    return 0;
}
