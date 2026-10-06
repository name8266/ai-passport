#pragma once

#include <stdint.h>

typedef struct {
    uint16_t stop_ms;
    uint16_t settle_ms;
    uint16_t tick_start_ms;
    uint16_t tick_end_ms;
    uint16_t phase_q8;
    uint16_t velocity_q8;
    uint16_t drag_q8;
    int8_t bounce_px;
} motion_slot_profile_t;

typedef struct {
    int16_t x_q8;
    int16_t y_q8;
    int16_t vx_q8;
    int16_t vy_q8;
    int16_t angle_tenths;
    int16_t omega_tenths;
    uint8_t restitution;
    uint8_t face_phase;
} motion_die_profile_t;

typedef struct {
    int16_t x_q8;
    int16_t y_q8;
    int16_t vx_q8;
    int16_t vy_q8;
    uint8_t gravity_q8;
    uint8_t peg_kick_q8;
    uint8_t restitution;
    uint8_t jitter_q8;
} motion_plinko_profile_t;

typedef struct {
    uint16_t duration_ms;
    uint16_t wheel_velocity_q8;
    uint16_t wheel_drag_q8;
    uint16_t ball_velocity_q8;
    uint16_t ball_drag_q8;
    uint8_t wheel_start;
    uint8_t ball_start;
} motion_roulette_profile_t;

motion_slot_profile_t motion_slot_profile(uint32_t seed, uint8_t speed, uint8_t reel);
motion_die_profile_t motion_die_profile(uint32_t seed, uint8_t die_index, uint8_t speed);
motion_plinko_profile_t motion_plinko_profile(uint32_t seed, uint8_t speed);
motion_roulette_profile_t motion_roulette_profile(uint32_t seed, uint8_t speed);
