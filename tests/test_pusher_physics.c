#include "pusher_physics.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>

static void progress(cp_game_t *g, unsigned frames)
{
    for (unsigned i = 0; i < frames; ++i) cp_step(g, 1.0f / 30.0f);
}

int main(void)
{
    cp_game_t g, other;
    cp_init(&g, 123456u, 120u, 50u, 70u);
    cp_init(&other, 123456u, 120u, 50u, 70u);
    assert(cp_coin_count(&g) == 54);
    assert(g.best == 50 && g.jackpot_pool == 70);
    cp_aim(&g, 1000);
    assert(g.drop_x == 196);
    cp_aim(&g, -1000);
    assert(g.drop_x == 12);
    cp_init(&g, 1u, 120u, 0u, 70u);
    cp_init(&other, 1u, 120u, 0u, 70u);
    assert(cp_drop(&g) && cp_drop(&other));
    assert(g.bank == 119 && g.jackpot_pool == 71);
    progress(&g, 120);
    progress(&other, 120);
    assert(g.rng == other.rng);
    assert(g.bank == other.bank && g.collected == other.collected);
    for (unsigned i = 0; i < CP_MAX_COINS; ++i) {
        cp_coin_t *c = &g.coins[i];
        cp_coin_t *d = &other.coins[i];
        assert(c->active == d->active);
        if (!c->active) continue;
        assert(isfinite(c->x) && isfinite(c->y) && isfinite(c->z));
        assert(fabsf(c->x - d->x) < 0.0001f);
        if (!c->falling) {
            assert(c->x >= c->radius - 0.001f);
            assert(c->x <= CP_BOARD_WIDTH - c->radius + 0.001f);
            assert(c->z >= 0);
        }
    }
    uint32_t bank = g.bank;
    g.paused = true;
    cp_step(&g, 1.0f);
    assert(!cp_drop(&g));
    assert(!cp_buy_chaos(&g));
    assert(g.bank == bank);
    g.paused = false;
    assert(cp_buy_chaos(&g));
    assert(g.bank == bank - 6);
    assert(g.event != CP_EVENT_NONE);
    cp_init(&g, 77u, 0u, 0u, 0u);
    assert(!cp_drop(&g));
    assert(cp_event_name(CP_EVENT_PORTAL) != NULL);
    puts("coin pusher physics: PASS");
    return 0;
}
