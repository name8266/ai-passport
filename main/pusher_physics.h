#pragma once
/*
 * Coin Pusher: deterministic, fixed-capacity 2.5-D cylinder physics.
 * Units are virtual millimetres. No heap allocation or platform dependencies.
 */
#include <stdbool.h>
#include <stdint.h>

#define CP_MAX_COINS 192
#define CP_MAX_BALLS 16
#define CP_PEG_ROWS 4
#define CP_PEG_COLS 8
#define CP_PIN_HEIGHT 64.0f
#define CP_BOARD_WIDTH 208.0f
#define CP_FRONT_EDGE 171.0f

typedef enum {
    CP_COIN_COPPER = 0,
    CP_COIN_GOLD,
    CP_COIN_RUBY,
    CP_COIN_GIANT
} cp_coin_kind_t;

typedef enum {
    CP_EVENT_NONE = 0,
    CP_EVENT_COIN_RAIN,
    CP_EVENT_MAGNET,
    CP_EVENT_EARTHQUAKE,
    CP_EVENT_GOLD_RUSH,
    CP_EVENT_MOON,
    CP_EVENT_PORTAL,
    CP_EVENT_BLACK_HOLE,
    CP_EVENT_JACKPOT,
    CP_EVENT_RAPID_FIRE
} cp_event_t;

typedef struct {
    float x, y, z;
    float vx, vy, vz;
    float radius, height, mass;
    cp_coin_kind_t kind;
    bool active;
    bool falling;
} cp_coin_t;

typedef struct {
    float x, y, vx, vy;
    cp_coin_kind_t kind;
    bool active;
} cp_ball_t;

typedef struct {
    cp_coin_t coins[CP_MAX_COINS];
    uint32_t rng;
    uint32_t bank, score, best, jackpot_pool;
    uint32_t drops, collected, lost, jackpot_hits;
    uint8_t combo;
    float drop_x, pusher_y, phase, event_left, combo_left;
    float game_time, event_clock;
    float boost_left, pulse_cooldown, last_progress;
    uint32_t pulses, automatic_releases;
    cp_event_t event;
    bool paused;
    /* Appended after the v2 save prefix for compatible migration. */
    cp_ball_t balls[CP_MAX_BALLS];
    uint32_t slot_hits[5], peg_hits;
    float rapid_clock;
    uint8_t last_slot;
    float slot_flash;
} cp_game_t;

void cp_init(cp_game_t *game, uint32_t seed, uint32_t bank,
             uint32_t best, uint32_t jackpot_pool);
void cp_aim(cp_game_t *game, float offset);
bool cp_drop(cp_game_t *game);
bool cp_buy_chaos(cp_game_t *game);
/* Temporary hydraulic overdrive: no coin removal or teleportation. */
bool cp_pulse(cp_game_t *game);
void cp_step(cp_game_t *game, float seconds);
unsigned cp_coin_count(const cp_game_t *game);
const char *cp_event_name(cp_event_t event);

unsigned cp_ball_count(const cp_game_t *game);
void cp_peg_position(unsigned row, unsigned col, float *x, float *y);
