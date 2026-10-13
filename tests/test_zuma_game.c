#include "zuma_game.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>

int main(void) {
    zuma_game_t g;
    zuma_init(&g, 1234);
    assert(g.mode == ZUMA_MENU);
    assert(g.path_count > 200 && g.path_length > 800);
    for (int i = 1; i < g.path_count; i++)
        assert(g.path[i].distance > g.path[i-1].distance);
    zuma_turn(&g, 1);
    assert(g.difficulty == 1);
    zuma_turn(&g, -1);
    assert(g.difficulty == 0);
    zuma_start(&g);
    assert(g.mode == ZUMA_PLAYING && g.count == 10 && g.quota == 25);
    for (int i = 2; i < g.count; i++)
        assert(g.balls[i] != g.balls[i-1] || g.balls[i] != g.balls[i-2]);
    float before = g.aim_radians;
    zuma_turn(&g, 1);
    assert(g.aim_radians > before);
    zuma_swap(&g);
    assert(zuma_take_sound(&g) == ZUMA_SOUND_SWAP);
    zuma_fire(&g);
    assert(g.shot.active);
    assert(zuma_take_sound(&g) == ZUMA_SOUND_SHOOT);
    zuma_toggle_pause(&g);
    float lead = g.lead;
    zuma_tick(&g, 0.05f);
    assert(g.lead == lead);
    zuma_toggle_pause(&g);
    zuma_tick(&g, 0.05f);
    assert(g.lead > lead);

    /* A shot inserted between two same-colour balls removes all three. */
    g.count = 2;
    g.balls[0] = 2; g.balls[1] = 2;
    g.lead = 100;
    g.ball_distance[0] = 100;
    g.ball_distance[1] = 87;
    g.shot.active = true;
    g.shot.color = 2;
    g.shot.vx = 0; g.shot.vy = 0;
    zuma_path_position(&g, g.lead, &g.shot.x, &g.shot.y);
    g.score = 0;
    zuma_tick(&g, 0);
    assert(g.count == 0 && g.score == 30);
    assert(zuma_take_sound(&g) == ZUMA_SOUND_MATCH);
    g.spawned = g.quota;
    zuma_tick(&g, 0);
    assert(g.mode == ZUMA_WON);
    assert(zuma_take_sound(&g) == ZUMA_SOUND_WIN);
    zuma_to_menu(&g);
    assert(g.mode == ZUMA_MENU);
    zuma_start(&g);
    g.count = 1;
    g.lead = g.path_length - 8;
    g.ball_distance[0] = g.lead;
    zuma_tick(&g, 0);
    assert(g.mode == ZUMA_LOST);

    /* Removed balls leave a gap; closure creates a second, scored match. */
    zuma_start(&g);
    g.count = 6;
    g.spawned = g.quota;
    g.lead = 120;
    const uint8_t colors[] = {0, 0, 1, 1, 0, 0};
    for (int i = 0; i < 6; i++) {
        g.balls[i] = colors[i];
        g.ball_distance[i] = 120 - 13 * i;
    }
    g.shot.active = true;
    g.shot.color = 1;
    g.shot.vx = 0; g.shot.vy = 0;
    zuma_path_position(&g, g.ball_distance[2], &g.shot.x, &g.shot.y);
    zuma_tick(&g, 0);
    assert(g.count == 4 && g.score == 30);
    assert(g.ball_distance[1] - g.ball_distance[2] > 25);
    for (int i = 0; i < 20 && g.mode == ZUMA_PLAYING; i++) zuma_tick(&g, 0.05f);
    assert(g.mode == ZUMA_WON && g.count == 0 && g.score == 110);

    /* Exercise long rounds with real turn, fire, and swap sequences. */
    for (uint32_t seed = 1; seed <= 25; seed++) {
        zuma_init(&g, seed);
        g.difficulty = seed % 3;
        zuma_start(&g);
        uint32_t rng = seed * 747796405u;
        for (int frame = 0; frame < 5000 && g.mode == ZUMA_PLAYING; frame++) {
            rng = rng * 1664525u + 1013904223u;
            if ((rng & 7u) == 0) zuma_turn(&g, rng & 128u ? 1 : -1);
            if ((rng & 15u) == 0) zuma_fire(&g);
            if ((rng & 127u) == 0) zuma_swap(&g);
            zuma_tick(&g, 0.033f);
            assert(g.count <= ZUMA_MAX_BALLS && g.spawned <= g.quota);
            assert(isfinite(g.lead) && isfinite(g.path_length));
            for (int i = 0; i < g.count; i++) {
                assert(g.balls[i] < 4 && isfinite(g.ball_distance[i]));
                if (i) assert(g.ball_distance[i - 1] >= g.ball_distance[i] - 0.01f);
            }
        }
        assert(g.mode == ZUMA_WON || g.mode == ZUMA_LOST);
    }
    puts("zuma core: PASS");
    return 0;
}
