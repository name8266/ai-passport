#include "kinetic_core.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static void valid(const kin_game_t *g)
{
    int targets = 0;
    int active = 0;
    for (int i = 0; i < KIN_MAX_BODIES; ++i) {
        const kin_body_t *b = &g->bodies[i];
        if (!b->alive) continue;
        ++active;
        assert(isfinite(b->x) && isfinite(b->y));
        assert(isfinite(b->vx) && isfinite(b->vy));
        assert(isfinite(b->angle) && isfinite(b->omega));
        assert(b->x >= KIN_LEFT + b->radius - 0.2f);
        assert(b->x <= KIN_RIGHT - b->radius + 0.2f);
        assert(b->y >= KIN_TOP + b->radius - 0.2f);
        assert(b->y <= KIN_FLOOR - b->radius + 0.2f);
        if (b->kind == KIN_GLASS) ++targets;
    }
    assert(active == kin_active_count(g));
    assert(targets == g->targets);
    assert(g->shots <= 5);
    assert(g->aim_deg >= 10 && g->aim_deg <= 65);
    assert(g->power <= 2);
    assert(g->phase <= KIN_LOST);
}

static void test_input_and_levels(void)
{
    kin_game_t game;
    kin_new_game(&game, 12345);
    assert(game.phase == KIN_PLAY);
    assert(game.stage == 1 && game.shots == 5);
    assert(game.targets >= 4);
    kin_aim(&game, -1000);
    assert(game.aim_deg == 10);
    kin_aim(&game, 1000);
    assert(game.aim_deg == 65);
    kin_power_next(&game);
    kin_power_next(&game);
    kin_power_next(&game);
    assert(game.power == 1);
    assert(kin_fire(&game));
    assert(game.shots == 4);
    assert(!kin_fire(&game)); /* cooldown prevents unintended double shot */
    game.paused = 1;
    uint32_t ticks = game.ticks;
    for (int i = 0; i < 60; ++i) kin_step(&game);
    assert(game.ticks == ticks);
    game.paused = 0;
    for (int i = 0; i < 23; ++i) kin_step(&game);
    assert(game.cooldown == 0);
    valid(&game);
}

static void test_gravity_and_boundaries(void)
{
    kin_game_t g = {0};
    g.phase = KIN_PLAY;
    g.shots = 1;
    g.aim_deg = 20;
    g.bodies[0] = (kin_body_t){
        .alive = 1, .kind = KIN_STONE, .x = 110, .y = 110,
        .radius = 8, .mass = 2.8f,
    };
    for (int i = 0; i < 15; ++i) kin_step(&g);
    assert(g.bodies[0].y > 120);
    assert(g.bodies[0].vy > 0);
    valid(&g);
    for (int i = 0; i < 1500; ++i) {
        kin_step(&g);
        valid(&g);
    }
    assert(fabsf(g.bodies[0].y - (KIN_FLOOR - 8)) < 0.1f);
}

static void test_real_collision_breaks_glass(void)
{
    kin_game_t g = {0};
    g.phase = KIN_PLAY;
    g.shots = 1;
    g.aim_deg = 20;
    g.targets = 1;
    g.bodies[0] = (kin_body_t){
        .alive = 1, .kind = KIN_PROJECTILE, .x = 100, .y = 190,
        .vx = 10, .radius = 7, .mass = 3.4f
    };
    g.bodies[1] = (kin_body_t){
        .alive = 1, .kind = KIN_GLASS, .x = 116, .y = 190,
        .radius = 8, .mass = 1.2f
    };
    for (int i = 0; i < 8 && g.phase == KIN_PLAY; ++i)
        kin_step(&g);
    assert(g.phase == KIN_WON);
    assert(g.targets == 0);
    assert(g.score >= 100);
    assert(g.hits >= 1);
    valid(&g);
    uint32_t score = g.score;
    kin_next_stage(&g);
    assert(g.stage == 1); /* stage starts at 0 in controlled test */
    assert(g.phase == KIN_PLAY);
    assert(g.score > score);
}

static void test_deterministic_stress(void)
{
    unsigned impacts = 0;
    for (unsigned seed = 1; seed <= 48; ++seed) {
        kin_game_t a, b;
        kin_new_game(&a, seed * 0x9e3779b9u);
        kin_new_game(&b, seed * 0x9e3779b9u);
        for (int tick = 0; tick < 850; ++tick) {
            if (tick % 155 == 0) {
                kin_aim(&a, ((int)seed % 5 - 2) * 5);
                kin_aim(&b, ((int)seed % 5 - 2) * 5);
                (void)kin_fire(&a);
                (void)kin_fire(&b);
            }
            kin_step(&a);
            kin_step(&b);
            if ((tick % 12) == 0) {
                valid(&a);
                assert(memcmp(&a, &b, sizeof(a)) == 0);
            }
        }
        impacts += a.hits;
    }
    printf("Physics stress: 48 deterministic seeds x 850 frames, %u breaks\n", impacts);
}

int main(void)
{
    test_input_and_levels();
    test_gravity_and_boundaries();
    test_real_collision_breaks_glass();
    test_deterministic_stress();
    puts("Gravity Siege host tests: PASS");
    return 0;
}
