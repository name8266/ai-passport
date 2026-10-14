/* Pachinko physics and scoring: pure C, no LVGL or ESP-IDF dependency. */
#pragma once
#include <stdbool.h>
#include <stdint.h>

#define PK_MAX_BALLS 6
#define PK_PEG_ROWS 5
#define PK_FRAME_MS 40
#define PK_BOARD_LEFT 13.0f
#define PK_BOARD_RIGHT 227.0f
#define PK_BOARD_BOTTOM 259.0f

typedef enum {
    PK_CLASSIC = 0,  /* 30 shots and permanent high score */
    PK_RUSH = 1,     /* 60 second sprint, unlimited shots */
    PK_SURVIVAL = 2, /* 12 shots, hazardous outer pockets */
    PK_MODE_COUNT = 3
} pk_mode_t;

typedef struct {
    float x, y, vx, vy;
    uint8_t hits;
    bool active;
    bool bumper_hit;
} pk_ball_t;

typedef struct {
    pk_ball_t balls[PK_MAX_BALLS];
    pk_mode_t mode;
    uint32_t rng;
    uint32_t score;
    uint32_t high_score[PK_MODE_COUNT];
    uint32_t ticks;
    uint32_t last_shot_tick;
    uint32_t last_nudge_tick;
    uint32_t shots;
    uint16_t fever_ticks;
    uint8_t energy;           /* 0..100 bumper charge */
    uint8_t reels[3];         /* 0..2, shown on central pocket */
    uint8_t combo;            /* 1..8 payout multiplier */
    int16_t aim_x;
    int16_t ammo;             /* -1 means unlimited */
    int16_t last_award;
    uint8_t last_slot;
    bool last_jackpot;
    bool finished;
} pk_state_t;

/* Board constants are deterministic, suitable for host tests and rendering. */
int pk_peg_count(int row);
void pk_peg_pos(int row, int col, float *x, float *y);
float pk_bumper_x(const pk_state_t *state);

/* All public functions operate on caller-owned state without allocation. */
void pk_init(pk_state_t *state, uint32_t seed);
void pk_start(pk_state_t *state, pk_mode_t mode);
void pk_aim(pk_state_t *state, int direction);
bool pk_fire(pk_state_t *state);
bool pk_nudge(pk_state_t *state);
void pk_step(pk_state_t *state);
int pk_active_balls(const pk_state_t *state);
