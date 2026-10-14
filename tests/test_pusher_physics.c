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
    assert(CP_MAX_COINS == 192);
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
    /* Payout is determined by physically crossing the lip and falling. */
    cp_init(&g, 77u, 90u, 0u, 0u);
    for (unsigned i = 1; i < CP_MAX_COINS; ++i) g.coins[i].active = false;
    g.coins[0].x = 104.0f;
    g.coins[0].y = CP_FRONT_EDGE + 1.0f;
    g.coins[0].z = 0.0f;
    g.coins[0].falling = true;
    g.coins[0].kind = CP_COIN_GOLD;
    progress(&g, 30);
    assert(g.collected == 1 && g.bank == 93);
    assert(!g.coins[0].active);
    cp_init(&g, 78u, 90u, 0u, 0u);
    for (unsigned i = 1; i < CP_MAX_COINS; ++i) g.coins[i].active = false;
    g.coins[0].x = 8.5f;
    g.coins[0].y = CP_FRONT_EDGE + 1.0f;
    g.coins[0].falling = true;
    progress(&g, 30);
    assert(g.lost == 1 && g.collected == 0 && g.bank == 90);
    /* Grounded coins must transmit pressure sideways, not jump into piles. */
    cp_init(&g, 80u, 90u, 0u, 0u);
    for (unsigned i = 2; i < CP_MAX_COINS; ++i) g.coins[i].active = false;
    g.coins[0].x = 98.0f; g.coins[0].y = 105.0f;
    g.coins[1].x = 111.0f; g.coins[1].y = 105.0f;
    g.coins[0].z = g.coins[1].z = 0.0f;
    cp_step(&g, 1.0f / 60.0f);
    assert(g.coins[0].x < 98.0f);
    assert(g.coins[1].x > 111.0f);
    assert(g.coins[0].z < 0.001f && g.coins[1].z < 0.001f);
    /* Reaching capacity must never deduct credits or soft-lock simulation. */
    cp_init(&g, 77u, 300u, 0u, 0u);
    for (unsigned i = 0; i < CP_MAX_COINS; ++i) {
        g.coins[i].active = true;
        g.coins[i].x = 20.0f + (float)(i % 9u) * 21.0f;
        g.coins[i].y = 28.0f + (float)(i / 9u) * 6.0f;
        g.coins[i].z = 0.0f;
        g.coins[i].radius = 8.2f;
        g.coins[i].height = 2.9f;
        g.coins[i].mass = 1.0f;
        g.coins[i].vx = g.coins[i].vy = g.coins[i].vz = 0.0f;
        g.coins[i].kind = CP_COIN_COPPER;
        g.coins[i].falling = false;
    }
    uint32_t full_bank = g.bank;
    assert(!cp_drop(&g));
    assert(g.bank == full_bank);
    assert(cp_pulse(&g));
    assert(g.boost_left > 0.0f && !cp_pulse(&g));
    progress(&g, 10);
    assert(isfinite(g.pusher_y));
    cp_init(&g, 500u, 100u, 0u, 0u);
    assert(cp_pulse(&g));
    assert(!cp_pulse(&g));
    progress(&g, 240);
    assert(g.boost_left == 0.0f && g.pulse_cooldown == 0.0f);
    assert(cp_pulse(&g));
    cp_init(&g, 77u, 0u, 0u, 0u);
    assert(!cp_drop(&g));
    assert(cp_event_name(CP_EVENT_PORTAL) != NULL);
    puts("coin pusher physics: PASS");
    return 0;
}
