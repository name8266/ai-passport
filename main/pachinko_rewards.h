/* Virtual arcade reward economy: pure C, no cash, I/O or dynamic allocation. */
#pragma once
#include <stdbool.h>
#include <stdint.h>

#define PK_POOL_SEED 500u
#define PK_POOL_CAP 99999u
#define PK_REEL_SYMBOLS 5u
#define PK_PITY_SPINS 12u

typedef enum {
    PK_PRIZE_NONE = 0,
    PK_PRIZE_PAIR,
    PK_PRIZE_MINI,
    PK_PRIZE_BIG,
    PK_PRIZE_SUPER,
    PK_PRIZE_JACKPOT,
    PK_PRIZE_MISSION
} pk_prize_t;

typedef enum {
    PK_GOAL_PEG = 0,
    PK_GOAL_CENTER,
    PK_GOAL_BUMPER
} pk_goal_t;

/* Versioned metadata is serialized through independently keyed NVS values. */
typedef struct {
    uint32_t pool;
    uint32_t lifetime_score;
    uint32_t total_jackpots;
    uint32_t medals;
} pk_reward_meta_t;

/* A fixed-size, independently testable reward state.
 * Progress for current mission and pity counter reset on a new round. */
typedef struct {
    pk_reward_meta_t meta;
    uint32_t spins;
    uint32_t event_seq;
    uint32_t event_points;
    uint16_t event_ticks;
    uint16_t mission_progress;
    uint16_t mission_target;
    uint8_t mission_stage;
    uint8_t no_big_streak;
    pk_goal_t mission_goal;
    pk_prize_t event;
} pk_rewards_t;

typedef struct {
    uint32_t points;
    uint8_t extra_balls;
    uint16_t fever_ticks;
    pk_prize_t prize;
} pk_reward_result_t;

void pk_rewards_init(pk_rewards_t *rewards);
void pk_rewards_new_round(pk_rewards_t *rewards);
void pk_rewards_import(pk_rewards_t *rewards, const pk_reward_meta_t *meta);
void pk_rewards_on_shot(pk_rewards_t *rewards);
void pk_rewards_tick(pk_rewards_t *rewards);
pk_reward_result_t pk_rewards_spin(pk_rewards_t *rewards,
                                   uint8_t symbols[3]);
pk_reward_result_t pk_rewards_record(pk_rewards_t *rewards,
                                     pk_goal_t event);
void pk_rewards_add_lifetime(pk_rewards_t *rewards, uint32_t points);
