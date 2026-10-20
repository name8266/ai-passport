#pragma once
#include <stdbool.h>
#include <stdint.h>

/* Fictional, standard competition parameters. SI internally; no platform APIs. */
#define BH_HZ 60
#define BH_TRACK_M 500.0f
#define BH_MIN_RANK_KMH 100.0f
typedef enum { BH_NONE = -1, BH_TENT, BH_BRAKE, BH_THROTTLE } bh_key_t;
typedef enum { BH_RUNNING, BH_STOPPED, BH_COLLISION, BH_IDLE, BH_TIMEOUT } bh_end_t;
enum { BH_EV_100 = 1, BH_EV_TENT = 2, BH_EV_WARNING = 4,
       BH_EV_BREAK = 8, BH_EV_NEAR = 16, BH_EV_END = 32 };
typedef struct {
    float position, speed, peak_speed, remaining;
    float pedal, throttle, tent, max_pedal, max_tent, brake_start_speed;
    float brake_force, tent_force, zero100_s;
    uint32_t ticks, fired;
    int pedal_thirds, tent_thirds, throttle_twelfths;
    uint16_t stopped_ticks;
    uint8_t obstacle;
    bool brake_failed, moved, braking, stop_committed;
    bh_end_t end;
} bh_state_t;
void bh_reset(bh_state_t *s, unsigned obstacle);
uint32_t bh_step(bh_state_t *s, bh_key_t key);
bool bh_rankable(const bh_state_t *s);
float bh_air_tent_force(float speed, float opening);
/* Canonical single key: lowest ADC window wins, never a combined control. */
bh_key_t bh_key_from_mask(unsigned mask);

/* Timestamped input: caller splits at edges, not at render frames. */
typedef struct { int64_t time_us; uint32_t fraction; bh_key_t key; } bh_clock_t;
void bh_clock_reset(bh_clock_t *c, int64_t now);
uint32_t bh_advance(bh_state_t *s, bh_clock_t *c, int64_t now);
void bh_clock_key(bh_clock_t *c, bh_key_t key);
