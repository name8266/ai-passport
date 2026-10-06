#include "chance_core.h"

uint8_t chance_die(uint32_t random_value)
{
    return (uint8_t)(random_value % 6u) + 1u;
}

chance_roulette_result_t chance_roulette(uint32_t random_value)
{
    chance_roulette_result_t result = {
        .number = (uint8_t)(random_value % 37u),
        .color = CHANCE_GREEN,
    };

    if (result.number == 0) return result;

    switch (result.number) {
    case 1: case 3: case 5: case 7: case 9:
    case 12: case 14: case 16: case 18:
    case 19: case 21: case 23: case 25: case 27:
    case 30: case 32: case 34: case 36:
        result.color = CHANCE_RED;
        break;
    default:
        result.color = CHANCE_BLACK;
        break;
    }
    return result;
}

bool chance_is_triple(const uint8_t reels[3])
{
    return reels && reels[0] == reels[1] && reels[1] == reels[2];
}

bool chance_is_pair(const uint8_t reels[3])
{
    if (!reels) return false;
    return reels[0] == reels[1] || reels[1] == reels[2] || reels[0] == reels[2];
}
