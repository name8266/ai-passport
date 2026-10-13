#include "arcade_game.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>

static void controls(void) {
    arcade_game_t g;
    arcade_init(&g, 1234);
    assert(g.mode == ARCADE_HOME);
    for (int kind = 0; kind < ARCADE_GAME_COUNT; kind++) {
        arcade_start(&g, (arcade_kind_t)kind);
        assert(g.mode == ARCADE_PLAYING);
        assert(g.kind == (arcade_kind_t)kind);
        assert(arcade_name(g.kind)[0] != 0);
        assert(arcade_hint(g.kind)[0] != 0);
        assert(arcade_goal(g.kind)[0] != 0);
        arcade_input(&g, ARCADE_HOME_REQUEST);
        assert(g.mode == ARCADE_HOME);
    }
    arcade_start(&g, ARCADE_BREAKOUT);
    assert(g.phase == 0 && g.angle == 120);
    arcade_input(&g, ARCADE_OK_DOWN);
    assert(g.phase == 1 && g.vy < 0);
    arcade_start(&g, ARCADE_TETRIS);
    int old_x = g.piece_x;
    arcade_input(&g, ARCADE_LEFT_DOWN);
    assert(g.piece_x == old_x - 1);
    arcade_input(&g, ARCADE_LEFT_UP);
    arcade_input(&g, ARCADE_OK_DOUBLE);
    int filled = 0;
    for (int y = 0; y < 18; y++) for (int x = 0; x < 10; x++) filled += !!g.grid[y][x];
    assert(filled == 4);
    assert(arcade_tetris_cell(0, 0, 0, 1));
    assert(!arcade_tetris_cell(0, 0, 0, 0));
    arcade_start(&g, ARCADE_SNAKE);
    arcade_input(&g, ARCADE_RIGHT_DOWN);
    assert(g.direction == 1);
    arcade_start(&g, ARCADE_FLAPPY);
    arcade_input(&g, ARCADE_OK_DOWN);
    assert(g.vy < 0);
    arcade_start(&g, ARCADE_LANDER);
    arcade_input(&g, ARCADE_OK_DOWN);
    arcade_tick(&g, 1.0f / 30, false);
    assert(g.fuel < 100);
    arcade_start(&g, ARCADE_RACER);
    arcade_input(&g, ARCADE_LEFT_DOWN);
    assert(g.lane == 0);
}

static void end_conditions(void) {
    arcade_game_t g;
    arcade_init(&g, 77);
    arcade_start(&g, ARCADE_BREAKOUT);
    g.score = 110; g.phase = 1;
    g.x = 28; g.y = 65; g.vx = 0; g.vy = -100;
    arcade_tick(&g, 0.01f, false);
    assert(g.mode == ARCADE_WON);

    arcade_start(&g, ARCADE_TETRIS);
    g.lines = 5; g.piece = 0; g.rotation = 0; g.piece_x = 3; g.piece_y = 16;
    for (int x = 0; x < 10; x++) g.grid[17][x] = (x >= 3 && x <= 6) ? 0 : 1;
    arcade_input(&g, ARCADE_OK_DOUBLE);
    assert(g.mode == ARCADE_WON && g.lines == 6);

    arcade_start(&g, ARCADE_DOODLE);
    g.score = 599; g.y = 100; g.vy = -100;
    arcade_tick(&g, 0.01f, false);
    assert(g.mode == ARCADE_WON);

    arcade_start(&g, ARCADE_SNAKE);
    g.score = 70; g.food_x = 6; g.food_y = 7;
    for (int i = 0; i < 4; i++) arcade_tick(&g, 0.05f, false);
    assert(g.mode == ARCADE_WON);

    arcade_start(&g, ARCADE_SHOOTER);
    g.score = 140;
    g.actors[0].y = 100;
    g.shots[0].active = true;
    g.shots[0].x = g.actors[0].x;
    g.shots[0].y = g.actors[0].y + 5;
    arcade_tick(&g, 0.01f, false);
    assert(g.mode == ARCADE_WON);

    arcade_start(&g, ARCADE_FLAPPY);
    g.score = 9; g.actors[0].x = g.x - 36; g.actors[0].y = g.y;
    arcade_tick(&g, 0.01f, false);
    assert(g.mode == ARCADE_WON);

    arcade_start(&g, ARCADE_LANDER);
    g.x = 120; g.y = 258; g.vx = 0; g.vy = 0; g.angle = 0;
    arcade_tick(&g, 0.01f, false);
    assert(g.mode == ARCADE_WON);

    arcade_start(&g, ARCADE_RACER);
    g.elapsed = 24.99f;
    arcade_tick(&g, 0.03f, false);
    assert(g.mode == ARCADE_WON);
    arcade_input(&g, ARCADE_HOME_REQUEST);
    assert(g.mode == ARCADE_HOME);
}

static void seeded_long_runs(void) {
    for (int kind = 0; kind < ARCADE_GAME_COUNT; kind++) {
        for (int seed = 1; seed <= 12; seed++) {
            arcade_game_t g;
            arcade_init(&g, (uint32_t)(seed * 499 + kind));
            arcade_start(&g, (arcade_kind_t)kind);
            for (int frame = 0; frame < 1800; frame++) {
                if (g.mode != ARCADE_PLAYING) arcade_start(&g, (arcade_kind_t)kind);
                arcade_tick(&g, 1.0f / 30.0f, true);
                assert(isfinite(g.x) && isfinite(g.y));
                assert(isfinite(g.vx) && isfinite(g.vy));
                assert(g.kind == (arcade_kind_t)kind);
                assert(g.snake_length >= 0 && g.snake_length <= 128);
                assert(g.piece >= 0 && g.piece < 7);
                assert(g.fuel >= 0 && g.fuel <= 100);
            }
        }
    }
}

int main(void) {
    controls();
    end_conditions();
    seeded_long_runs();
    puts("eight arcade games: controls, eight win paths and 96 seeded 60-second runs passed");
}
