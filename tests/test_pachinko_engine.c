#include "pachinko_engine.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void test_init_and_modes(void)
{
    pk_state_t s;
    pk_init(&s, 123);
    assert(s.mode == PK_CLASSIC && s.ammo == 30 && s.aim_x == 120);
    assert(PK_FRAME_MS == 40 && PK_MAX_BALLS == 6);
    assert(pk_peg_count(0) == 7 && pk_peg_count(1) == 6);
    assert(pk_peg_count(-1) == 0 && pk_peg_count(5) == 0);
    pk_aim(&s, -100);
    assert(s.aim_x == 111);
    for (int i = 0; i < 40; ++i) pk_aim(&s, -1);
    assert(s.aim_x == 38);
    for (int i = 0; i < 40; ++i) pk_aim(&s, 1);
    assert(s.aim_x == 202);

    s.high_score[PK_CLASSIC] = 5432;
    pk_start(&s, PK_RUSH);
    assert(s.high_score[PK_CLASSIC] == 5432);
    assert(s.ammo == -1 && s.score == 0 && s.ticks == 0);
    pk_start(&s, PK_SURVIVAL);
    assert(s.ammo == 12 && s.combo == 1);
    assert(pk_bumper_x(&s) >= 75.0f);
}

static void test_shooting_and_physics(void)
{
    pk_state_t s;
    pk_init(&s, 42);
    assert(pk_fire(&s));
    assert(s.ammo == 29 && pk_active_balls(&s) == 1);
    assert(!pk_fire(&s)); /* cooldown */
    for (int i = 0; i < 6; ++i) pk_step(&s);
    assert(pk_fire(&s));
    assert(pk_active_balls(&s) == 2);
    /* All balls must settle without tunnelling forever or leaving screen. */
    int loops = 0;
    while (pk_active_balls(&s) && loops < 2000) {
        pk_step(&s);
        ++loops;
        for (int b = 0; b < PK_MAX_BALLS; ++b) {
            if (s.balls[b].active) {
                assert(s.balls[b].x >= PK_BOARD_LEFT - 0.1f);
                assert(s.balls[b].x <= PK_BOARD_RIGHT + 0.1f);
            }
        }
    }
    assert(loops < 2000);
    assert(pk_active_balls(&s) == 0);
    assert(s.score == s.high_score[PK_CLASSIC]);
    assert(s.score > 0);
}

static void test_rush_timeout(void)
{
    pk_state_t s;
    pk_init(&s, 3);
    pk_start(&s, PK_RUSH);
    for (int i = 0; i < 1500; ++i) pk_step(&s);
    assert(s.finished);
    assert(!pk_fire(&s));
    pk_start(&s, PK_RUSH);
    assert(pk_fire(&s));
}

static void test_nudge_and_fever(void)
{
    pk_state_t s;
    pk_init(&s, 12);
    assert(!pk_nudge(&s));
    assert(pk_fire(&s));
    s.energy = 100;
    float old_vx = s.balls[0].vx;
    assert(pk_nudge(&s));
    assert(s.energy == 75);
    assert(s.balls[0].vx != old_vx);
    assert(!pk_nudge(&s));
    s.fever_ticks = 10;
    pk_step(&s);
    assert(s.fever_ticks == 9);
}

static void test_seed_reproducibility(void)
{
    pk_state_t a, b;
    pk_init(&a, 987654);
    pk_init(&b, 987654);
    assert(pk_fire(&a) && pk_fire(&b));
    for (int i = 0; i < 220; ++i) {
        pk_step(&a);
        pk_step(&b);
    }
    assert(a.score == b.score && a.last_slot == b.last_slot);
    assert(a.rng == b.rng && a.energy == b.energy);
    assert(a.shots == b.shots);
}

int main(void)
{
    test_init_and_modes();
    test_shooting_and_physics();
    test_rush_timeout();
    test_nudge_and_fever();
    test_seed_reproducibility();
    puts("pachinko engine tests: PASS");
    return 0;
}
