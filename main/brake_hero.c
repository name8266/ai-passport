#include "brake_hero.h"
#include <math.h>
#include <string.h>

static int clampi(int x, int top) { return x < 0 ? 0 : (x > top ? top : x); }
static float minf(float a, float b) { return a < b ? a : b; }
void bh_reset(bh_state_t *s, unsigned obstacle) {
    memset(s, 0, sizeof(*s));
    s->remaining = BH_TRACK_M;
    s->obstacle = obstacle % 4;
    s->zero100_s = -1;
}
bh_key_t bh_key_from_mask(unsigned mask) {
    for (unsigned i = 0; i < 3; ++i) if (mask & (1u << i)) return (bh_key_t)i;
    return BH_NONE;
}
float bh_air_tent_force(float v, float opening) {
    return 0.5f * 1.225f * 20.0f * (opening * 0.01f) * v * v;
}
uint32_t bh_step(bh_state_t *s, bh_key_t key) {
    if (s->end != BH_RUNNING) return 0;
    const float dt = 1.0f / BH_HZ;
    ++s->ticks;
    /* Exact integer ramps: 120 brake steps = exactly 1600 N, never rounded up. */
    s->tent_thirds = clampi(s->tent_thirds + (key == BH_TENT ? 2 : -1), 300);
    s->throttle_twelfths = s->stop_committed ? 0 :
        clampi(s->throttle_twelfths + (key == BH_THROTTLE ? 13 : -9), 1200);
    if (!s->brake_failed) {
        s->pedal_thirds += key == BH_BRAKE ? 40 : -60;
        if (s->pedal_thirds < 0) s->pedal_thirds = 0;
    }
    s->pedal = s->pedal_thirds / 3.0f;
    s->tent = s->tent_thirds / 3.0f;
    s->throttle = s->throttle_twelfths / 12.0f;
    if (s->pedal > s->max_pedal) s->max_pedal = s->pedal;
    if (s->tent > s->max_tent) s->max_tent = s->tent;
    uint32_t ev = 0;
    if (!s->brake_failed && s->pedal_thirds > 4800) {
        s->brake_failed = true;
        ev |= BH_EV_BREAK;
    }
    if (!s->braking && key == BH_BRAKE && s->speed > 0.5f) {
        s->braking = true;
        s->brake_start_speed = s->speed;
    }
    const float v = s->speed;
    const float ratio = v / 50.0f;
    const float r2 = ratio * ratio;
    const float limiter = fmaxf(0, 1 - r2 * r2);
    const float engine = s->throttle * 0.01f * limiter * minf(17500, 400000 / fmaxf(1, v));
    const float grip = 0.92f * 9.81f * 2400;
    s->brake_force = s->brake_failed ? 0 : minf(grip, grip * (s->pedal / 1600.0f));
    s->tent_force = bh_air_tent_force(v, s->tent);
    const float aero = 0.5f * 1.225f * 0.82f * v * v;
    const float accel = (engine - s->brake_force - s->tent_force - aero - 350) / 2400;
    float next = fmaxf(0, v + accel * dt);
    /* Integrate only until zero when braking crosses zero within this step. */
    float travel = (v + next) * 0.5f * dt;
    if (accel < 0 && v + accel * dt < 0) travel = v * v / (-2 * accel);
    s->speed = next;
    s->position += travel;
    s->remaining = fmaxf(0, BH_TRACK_M - s->position);
    if (next > s->peak_speed) s->peak_speed = next;
    if (next > 0.5f) s->moved = true;
    if (s->peak_speed * 3.6f >= BH_MIN_RANK_KMH && s->zero100_s < 0) {
        s->zero100_s = s->ticks * dt;
        ev |= BH_EV_100;
    }
    if (s->tent >= 20) ev |= BH_EV_TENT;
    if (s->pedal_thirds >= 4200) ev |= BH_EV_WARNING;
    if (s->remaining < 130 && s->speed > 20) ev |= BH_EV_NEAR;
    /* Collision wins over stopping, including a crossing in the final step. */
    if (s->position >= BH_TRACK_M) {
        s->position = BH_TRACK_M;
        s->end = BH_COLLISION;
    } else {
        if (s->moved && next <= 0.12f && s->throttle_twelfths == 0) {
            s->stop_committed = true;
            ++s->stopped_ticks;
        }
        else s->stopped_ticks = 0;
        if (s->stopped_ticks >= 36) { s->end = BH_STOPPED; s->speed = 0; }
        if (!s->moved && s->ticks >= 8 * BH_HZ) s->end = BH_IDLE;
        if (s->ticks >= 60 * BH_HZ && s->end == BH_RUNNING) s->end = BH_TIMEOUT;
    }
    if (s->end != BH_RUNNING) ev |= BH_EV_END;
    ev &= ~s->fired;
    s->fired |= ev;
    return ev;
}
bool bh_rankable(const bh_state_t *s) {
    return s->end == BH_STOPPED && s->moved && s->position < BH_TRACK_M &&
           isfinite(s->peak_speed) && s->peak_speed * 3.6f >= BH_MIN_RANK_KMH;
}
void bh_clock_reset(bh_clock_t *c, int64_t now) { *c = (bh_clock_t){ .time_us = now, .key = BH_NONE }; }
uint32_t bh_advance(bh_state_t *s, bh_clock_t *c, int64_t now) {
    if (now <= c->time_us) return 0;
    /* Runtime supplies short intervals. Bound suspension recovery to 60 s. */
    int64_t delta = now - c->time_us;
    c->time_us = now;
    if (delta > 60000000) delta = 60000000;
    uint64_t total = c->fraction + (uint64_t)delta * BH_HZ;
    uint32_t ev = 0;
    while (total >= 1000000) { ev |= bh_step(s, c->key); total -= 1000000; }
    c->fraction = (uint32_t)total;
    return ev;
}
void bh_clock_key(bh_clock_t *c, bh_key_t key) { c->key = key; }
