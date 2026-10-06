#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "tiny3d_math.h"

#define RIGID_DIE_HALF_Q8 (12 << 8)

typedef struct {
    t3_vec3_t pos_q8;
    t3_vec3_t vel_q8;
    t3_vec3_t omega_q10;
    t3_quat_t orientation;
    uint8_t restitution_q8;
    uint8_t friction_q8;
    uint8_t contacts;
    uint16_t sleep_ticks;
    bool sleeping;
} rigid_die_t;

void rigid_die_init(rigid_die_t *die, uint32_t seed, uint8_t index, uint8_t speed);
uint16_t rigid_die_step_60hz(rigid_die_t *die);
uint16_t rigid_die_pair_step(rigid_die_t *a, rigid_die_t *b);
uint8_t rigid_die_top_face(const rigid_die_t *die);
