#include "motion_core.h"

static uint32_t mix32(uint32_t x)
{
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}

static uint32_t range(uint32_t *state, uint32_t min, uint32_t max)
{
    *state = mix32(*state + 0x9e3779b9u);
    if (max <= min) return min;
    return min + (*state % (max - min + 1u));
}

motion_slot_profile_t motion_slot_profile(uint32_t seed, uint8_t speed, uint8_t reel)
{
    uint32_t s = mix32(seed ^ (0xA511E9B3u + (uint32_t)reel * 0x632BE59Bu));
    static const uint16_t base_stop[3] = { 1150, 820, 520 };
    static const uint16_t extra_stop[3] = { 620, 460, 300 };
    static const uint16_t tick_lo[3] = { 48, 36, 24 };
    static const uint16_t tick_hi[3] = { 74, 54, 38 };
    if (speed > 2) speed = 1;

    motion_slot_profile_t p = {
        .stop_ms = (uint16_t)(base_stop[speed] + reel * range(&s, 130, 240) +
                              range(&s, 0, extra_stop[speed])),
        .settle_ms = (uint16_t)range(&s, 110, 230),
        .tick_start_ms = (uint16_t)range(&s, tick_lo[speed], tick_hi[speed]),
        .tick_end_ms = (uint16_t)range(&s, 105, 175),
        .phase_q8 = (uint16_t)range(&s, 0, 255),
        .velocity_q8 = (uint16_t)range(&s, speed == 2 ? 330 : 240,
                                           speed == 0 ? 390 : 520),
        .drag_q8 = (uint16_t)range(&s, 2, 7),
        .bounce_px = (int8_t)range(&s, 2, 6),
    };
    return p;
}

motion_die_profile_t motion_die_profile(uint32_t seed, uint8_t die_index, uint8_t speed)
{
    uint32_t s = mix32(seed ^ (0x6D2B79F5u + (uint32_t)die_index * 0x27D4EB2Du));
    if (speed > 2) speed = 1;
    int direction = range(&s, 0, 1) ? 1 : -1;
    int spin_direction = range(&s, 0, 1) ? 1 : -1;

    motion_die_profile_t p = {
        .x_q8 = (int32_t)(range(&s, die_index ? 108 : 12, die_index ? 132 : 42) << 8),
        .y_q8 = (int32_t)(range(&s, 5, 25) << 8),
        .vx_q8 = (int32_t)(direction * (int)range(&s, speed == 0 ? 110 : 150,
                                                       speed == 2 ? 330 : 260)),
        .vy_q8 = (int32_t)range(&s, speed == 0 ? 120 : 160,
                                      speed == 2 ? 330 : 260),
        .angle_tenths = (int16_t)range(&s, 0, 3599),
        .omega_tenths = (int16_t)(spin_direction * (int)range(&s, 110, speed == 2 ? 310 : 240)),
        .restitution = (uint8_t)range(&s, 132, 188),
        .face_phase = (uint8_t)range(&s, 0, 5),
    };
    return p;
}

motion_plinko_profile_t motion_plinko_profile(uint32_t seed, uint8_t speed)
{
    uint32_t s = mix32(seed ^ 0xB5297A4Du);
    if (speed > 2) speed = 1;
    int direction = range(&s, 0, 1) ? 1 : -1;

    motion_plinko_profile_t p = {
        .x_q8 = (int32_t)(range(&s, 94, 106) << 8),
        .y_q8 = (int32_t)(3 << 8),
        .vx_q8 = (int32_t)(direction * (int)range(&s, 10, 48)),
        .vy_q8 = (int32_t)range(&s, 18, 42),
        .gravity_q8 = (uint8_t)range(&s, speed == 0 ? 7 : 10, speed == 2 ? 18 : 14),
        .peg_kick_q8 = (uint8_t)range(&s, 48, 92),
        .restitution = (uint8_t)range(&s, 82, 126),
        .jitter_q8 = (uint8_t)range(&s, 18, 54),
    };
    return p;
}

motion_roulette_profile_t motion_roulette_profile(uint32_t seed, uint8_t speed)
{
    uint32_t s = mix32(seed ^ 0x1B56C4E9u);
    if (speed > 2) speed = 1;
    static const uint16_t duration_lo[3] = { 2200, 1550, 900 };
    static const uint16_t duration_hi[3] = { 3400, 2500, 1550 };

    motion_roulette_profile_t p = {
        .duration_ms = (uint16_t)range(&s, duration_lo[speed], duration_hi[speed]),
        .wheel_velocity_q8 = (uint16_t)range(&s, 320, 560),
        .wheel_drag_q8 = (uint16_t)range(&s, 2, 5),
        .ball_velocity_q8 = (uint16_t)range(&s, 420, 720),
        .ball_drag_q8 = (uint16_t)range(&s, 3, 8),
        .wheel_start = (uint8_t)range(&s, 0, 15),
        .ball_start = (uint8_t)range(&s, 0, 15),
    };
    return p;
}
