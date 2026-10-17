#pragma once
#include <stdbool.h>
#include <stdint.h>

/* Standalone deterministic 2D circle rigid-body solver.
 * World units are pixels; fixed step is 1/60 s. No ESP-IDF or LVGL dependency. */
#define KIN_MAX_BODIES 28
#define KIN_LEFT 8.0f
#define KIN_RIGHT 232.0f
#define KIN_TOP 52.0f
#define KIN_FLOOR 270.0f

typedef enum {
    KIN_NONE = 0,
    KIN_PROJECTILE = 1,
    KIN_GLASS = 2,
    KIN_STONE = 3,
} kin_kind_t;

typedef struct {
    float x, y, vx, vy, angle, omega;
    float radius, mass;
    uint8_t kind;
    uint8_t alive;
} kin_body_t;

typedef enum {
    KIN_PLAY = 0,
    KIN_WON = 1,
    KIN_LOST = 2,
} kin_phase_t;

typedef struct {
    kin_body_t bodies[KIN_MAX_BODIES];
    uint32_t rng;
    uint32_t ticks;
    uint32_t score;
    uint16_t combo;
    uint16_t combo_ticks;
    uint16_t hits;
    uint8_t stage;
    uint8_t shots;
    uint8_t aim_deg;
    uint8_t power;
    uint8_t cooldown;
    uint8_t flash;
    uint8_t targets;
    uint8_t paused;
    uint8_t phase;
    float flash_x, flash_y;
} kin_game_t;

void kin_new_game(kin_game_t *game, uint32_t seed);
void kin_next_stage(kin_game_t *game);
void kin_aim(kin_game_t *game, int delta);
void kin_power_next(kin_game_t *game);
bool kin_fire(kin_game_t *game);
void kin_step(kin_game_t *game);
uint8_t kin_active_count(const kin_game_t *game);
