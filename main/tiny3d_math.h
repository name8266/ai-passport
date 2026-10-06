#pragma once
#include <stdint.h>

#define T3_Q14_ONE 16384

typedef struct { int32_t x, y, z; } t3_vec3_t;
typedef struct { int32_t w, x, y, z; } t3_quat_t;

uint32_t t3_isqrt_u64(uint64_t value);
t3_quat_t t3_quat_identity(void);
t3_quat_t t3_quat_normalize(t3_quat_t q);
t3_quat_t t3_quat_from_seed(uint32_t seed);
void t3_quat_integrate_60hz(t3_quat_t *q, t3_vec3_t omega_q10);
t3_vec3_t t3_quat_rotate_q8(t3_quat_t q, t3_vec3_t v_q8);
t3_vec3_t t3_quat_rotate_q14(t3_quat_t q, t3_vec3_t v_q14);
int32_t t3_dot_q14(t3_vec3_t a_q14, t3_vec3_t b_q14);
