#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    ARCADE_BREAKOUT, ARCADE_TETRIS, ARCADE_DOODLE, ARCADE_SNAKE,
    ARCADE_SHOOTER, ARCADE_FLAPPY, ARCADE_LANDER, ARCADE_RACER,
    ARCADE_DOCKING,
    ARCADE_GAME_COUNT
} arcade_kind_t;

typedef enum { ARCADE_HOME, ARCADE_PLAYING, ARCADE_WON, ARCADE_LOST } arcade_mode_t;
typedef enum {
    ARCADE_LEFT_DOWN, ARCADE_LEFT_UP, ARCADE_RIGHT_DOWN, ARCADE_RIGHT_UP,
    ARCADE_OK_DOWN, ARCADE_OK_UP, ARCADE_OK_CLICK, ARCADE_OK_DOUBLE,
    ARCADE_HOME_REQUEST
} arcade_action_t;

typedef struct { float x, y, vx, vy; uint8_t kind; bool active; } arcade_actor_t;

typedef struct {
    arcade_kind_t kind;
    arcade_mode_t mode;
    uint32_t rng, score;
    int lives;
    float elapsed, accumulator, move_clock, x, y, vx, vy, angle, fuel;
    int direction, held, lane, lines, phase, piece, rotation, piece_x, piece_y;
    /* Spacecraft docking: six inertial control axes, metre and radian units. */
    float dock_x, dock_y, dock_z, dock_vx, dock_vy, dock_vz;
    float dock_yaw, dock_pitch, dock_roll, dock_wyaw, dock_wpitch, dock_wroll;
    float dock_propellant;
    int dock_axis;
    bool ok_held;
    uint8_t bricks[5][6];
    uint8_t grid[18][10];
    int8_t snake_x[128], snake_y[128];
    int snake_length, food_x, food_y;
    arcade_actor_t actors[10];
    arcade_actor_t shots[4];
} arcade_game_t;

void arcade_init(arcade_game_t *g, uint32_t seed);
void arcade_start(arcade_game_t *g, arcade_kind_t kind);
void arcade_input(arcade_game_t *g, arcade_action_t action);
void arcade_tick(arcade_game_t *g, float dt, bool demo);
bool arcade_tetris_cell(int piece, int rotation, int x, int y);
const char *arcade_name(arcade_kind_t kind);
const char *arcade_hint(arcade_kind_t kind);
const char *arcade_goal(arcade_kind_t kind);
/* Lander reserves OK for continuous thrust; its exit gesture uses UP instead. */
bool arcade_long_exit_uses_up(arcade_kind_t kind);
