#include "tiny3d_math.h"

static uint32_t mix32(uint32_t x)
{
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}

uint32_t t3_isqrt_u64(uint64_t value)
{
    uint64_t res = 0;
    uint64_t bit = (uint64_t)1 << 62;
    while (bit > value) bit >>= 2;
    while (bit) {
        if (value >= res + bit) {
            value -= res + bit;
            res = (res >> 1) + bit;
        } else {
            res >>= 1;
        }
        bit >>= 2;
    }
    return (uint32_t)res;
}

t3_quat_t t3_quat_identity(void)
{
    t3_quat_t q = { T3_Q14_ONE, 0, 0, 0 };
    return q;
}

t3_quat_t t3_quat_normalize(t3_quat_t q)
{
    uint64_t sum = (uint64_t)((int64_t)q.w * q.w) +
                   (uint64_t)((int64_t)q.x * q.x) +
                   (uint64_t)((int64_t)q.y * q.y) +
                   (uint64_t)((int64_t)q.z * q.z);
    uint32_t mag = t3_isqrt_u64(sum);
    if (mag < 64u) return t3_quat_identity();

    q.w = (int32_t)((int64_t)q.w * T3_Q14_ONE / (int32_t)mag);
    q.x = (int32_t)((int64_t)q.x * T3_Q14_ONE / (int32_t)mag);
    q.y = (int32_t)((int64_t)q.y * T3_Q14_ONE / (int32_t)mag);
    q.z = (int32_t)((int64_t)q.z * T3_Q14_ONE / (int32_t)mag);
    return q;
}

t3_quat_t t3_quat_from_seed(uint32_t seed)
{
    int32_t a[4];
    uint32_t s = seed;
    for (int i = 0; i < 4; ++i) {
        s = mix32(s + 0x9e3779b9u);
        a[i] = (int32_t)(s & 0x7fffu) - 16384;
    }
    t3_quat_t q = { a[0], a[1], a[2], a[3] };
    return t3_quat_normalize(q);
}

void t3_quat_integrate_60hz(t3_quat_t *q, t3_vec3_t omega_q10)
{
    if (!q) return;

    int64_t dw = -(int64_t)q->x * omega_q10.x -
                 (int64_t)q->y * omega_q10.y -
                 (int64_t)q->z * omega_q10.z;
    int64_t dx =  (int64_t)q->w * omega_q10.x +
                  (int64_t)q->y * omega_q10.z -
                  (int64_t)q->z * omega_q10.y;
    int64_t dy =  (int64_t)q->w * omega_q10.y +
                  (int64_t)q->z * omega_q10.x -
                  (int64_t)q->x * omega_q10.z;
    int64_t dz =  (int64_t)q->w * omega_q10.z +
                  (int64_t)q->x * omega_q10.y -
                  (int64_t)q->y * omega_q10.x;

    q->w += (int32_t)((dw >> 10) / 120);
    q->x += (int32_t)((dx >> 10) / 120);
    q->y += (int32_t)((dy >> 10) / 120);
    q->z += (int32_t)((dz >> 10) / 120);
    *q = t3_quat_normalize(*q);
}

static t3_vec3_t rotate(t3_quat_t q, t3_vec3_t v)
{
    int64_t xx = (int64_t)q.x * q.x;
    int64_t yy = (int64_t)q.y * q.y;
    int64_t zz = (int64_t)q.z * q.z;
    int64_t xy = (int64_t)q.x * q.y;
    int64_t xz = (int64_t)q.x * q.z;
    int64_t yz = (int64_t)q.y * q.z;
    int64_t wx = (int64_t)q.w * q.x;
    int64_t wy = (int64_t)q.w * q.y;
    int64_t wz = (int64_t)q.w * q.z;

    int32_t m00 = T3_Q14_ONE - (int32_t)((2 * (yy + zz)) >> 14);
    int32_t m01 = (int32_t)((2 * (xy - wz)) >> 14);
    int32_t m02 = (int32_t)((2 * (xz + wy)) >> 14);
    int32_t m10 = (int32_t)((2 * (xy + wz)) >> 14);
    int32_t m11 = T3_Q14_ONE - (int32_t)((2 * (xx + zz)) >> 14);
    int32_t m12 = (int32_t)((2 * (yz - wx)) >> 14);
    int32_t m20 = (int32_t)((2 * (xz - wy)) >> 14);
    int32_t m21 = (int32_t)((2 * (yz + wx)) >> 14);
    int32_t m22 = T3_Q14_ONE - (int32_t)((2 * (xx + yy)) >> 14);

    t3_vec3_t out = {
        (int32_t)(((int64_t)m00 * v.x + (int64_t)m01 * v.y + (int64_t)m02 * v.z) >> 14),
        (int32_t)(((int64_t)m10 * v.x + (int64_t)m11 * v.y + (int64_t)m12 * v.z) >> 14),
        (int32_t)(((int64_t)m20 * v.x + (int64_t)m21 * v.y + (int64_t)m22 * v.z) >> 14),
    };
    return out;
}

t3_vec3_t t3_quat_rotate_q8(t3_quat_t q, t3_vec3_t v_q8)
{
    return rotate(q, v_q8);
}

t3_vec3_t t3_quat_rotate_q14(t3_quat_t q, t3_vec3_t v_q14)
{
    return rotate(q, v_q14);
}

int32_t t3_dot_q14(t3_vec3_t a, t3_vec3_t b)
{
    return (int32_t)(((int64_t)a.x * b.x +
                      (int64_t)a.y * b.y +
                      (int64_t)a.z * b.z) >> 14);
}
