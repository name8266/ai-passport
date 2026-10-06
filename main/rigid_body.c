#include "rigid_body.h"
#include <limits.h>

static uint32_t mix32(uint32_t x)
{
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}

static int32_t iabs32(int32_t v) { return v < 0 ? -v : v; }

static int32_t range_s(uint32_t *s, int32_t lo, int32_t hi)
{
    *s = mix32(*s + 0x9e3779b9u);
    return lo + (int32_t)(*s % (uint32_t)(hi - lo + 1));
}

static t3_vec3_t vertex_local_q8(int index)
{
    const int32_t h = RIGID_DIE_HALF_Q8;
    t3_vec3_t v = {
        (index & 1) ? h : -h,
        (index & 2) ? h : -h,
        (index & 4) ? h : -h,
    };
    return v;
}

void rigid_die_init(rigid_die_t *d, uint32_t seed, uint8_t index, uint8_t speed)
{
    if (!d) return;
    if (speed > 2) speed = 1;

    uint32_t s = mix32(seed ^ (0x51ed270bu + (uint32_t)index * 0x68bc21ebu));
    d->pos_q8.x = (index ? 31 : -31) << 8;
    d->pos_q8.y = range_s(&s, 45, 62) << 8;
    d->pos_q8.z = range_s(&s, -13, 13) << 8;
    d->vel_q8.x = range_s(&s, index ? -95 : 50, index ? -45 : 100);
    d->vel_q8.y = range_s(&s, speed == 0 ? 50 : 70, speed == 2 ? 125 : 105);
    d->vel_q8.z = range_s(&s, -55, 55);
    d->omega_q10.x = range_s(&s, -4800, 4800);
    d->omega_q10.y = range_s(&s, -4200, 4200);
    d->omega_q10.z = range_s(&s, -5200, 5200);
    d->orientation = t3_quat_from_seed(s ^ 0xA5A5D1CEu);
    d->restitution_q8 = (uint8_t)range_s(&s, 112, 154);
    d->friction_q8 = (uint8_t)range_s(&s, 34, 62);
    d->contacts = 0;
    d->sleep_ticks = 0;
    d->sleeping = false;
}

static t3_vec3_t cross_omega_r_per_tick(t3_vec3_t omega_q10, t3_vec3_t r_q8)
{
    t3_vec3_t c = {
        (int32_t)(((int64_t)omega_q10.y * r_q8.z - (int64_t)omega_q10.z * r_q8.y) / (1024 * 60)),
        (int32_t)(((int64_t)omega_q10.z * r_q8.x - (int64_t)omega_q10.x * r_q8.z) / (1024 * 60)),
        (int32_t)(((int64_t)omega_q10.x * r_q8.y - (int64_t)omega_q10.y * r_q8.x) / (1024 * 60)),
    };
    return c;
}

uint16_t rigid_die_step_60hz(rigid_die_t *d)
{
    if (!d || d->sleeping) return 0;

    d->vel_q8.y -= 15;
    d->pos_q8.x += d->vel_q8.x;
    d->pos_q8.y += d->vel_q8.y;
    d->pos_q8.z += d->vel_q8.z;
    t3_quat_integrate_60hz(&d->orientation, d->omega_q10);

    int32_t min_y = INT32_MAX;
    int lowest = 0;
    t3_vec3_t rotated[8];
    for (int i = 0; i < 8; ++i) {
        rotated[i] = t3_quat_rotate_q8(d->orientation, vertex_local_q8(i));
        int32_t y = d->pos_q8.y + rotated[i].y;
        if (y < min_y) {
            min_y = y;
            lowest = i;
        }
    }

    uint16_t impact_out = 0;
    if (min_y < 0) {
        d->pos_q8.y -= min_y;
        ++d->contacts;

        for (int i = 0; i < 8; ++i) {
            int32_t world_y = d->pos_q8.y + rotated[i].y;
            if (world_y > (2 << 8)) continue;

            t3_vec3_t point_v = cross_omega_r_per_tick(d->omega_q10, rotated[i]);
            point_v.x += d->vel_q8.x;
            point_v.y += d->vel_q8.y;
            point_v.z += d->vel_q8.z;
            if (point_v.y >= 0) continue;

            int32_t impact = -point_v.y;
            if (impact > impact_out) {
                impact_out = (uint16_t)(impact > 65535 ? 65535 : impact);
            }

            int32_t bounce = impact * (256 + d->restitution_q8) / 256;
            d->vel_q8.y += bounce / 2 + 1;

            d->omega_q10.x += (int32_t)((int64_t)rotated[i].z * bounce / 1500);
            d->omega_q10.z -= (int32_t)((int64_t)rotated[i].x * bounce / 1500);

            int32_t keep = 256 - d->friction_q8;
            d->vel_q8.x = d->vel_q8.x * keep / 256;
            d->vel_q8.z = d->vel_q8.z * keep / 256;
            d->omega_q10.y = d->omega_q10.y * 244 / 256;
        }

        if (lowest >= 0) {
            d->omega_q10.x = d->omega_q10.x * 252 / 256;
            d->omega_q10.z = d->omega_q10.z * 252 / 256;
        }
    }

    const int32_t xlim = 70 << 8;
    const int32_t zlim = 42 << 8;
    if (d->pos_q8.x < -xlim || d->pos_q8.x > xlim) {
        d->pos_q8.x = d->pos_q8.x < 0 ? -xlim : xlim;
        d->vel_q8.x = -d->vel_q8.x * 3 / 5;
        d->omega_q10.z = -d->omega_q10.z * 7 / 8;
    }
    if (d->pos_q8.z < -zlim || d->pos_q8.z > zlim) {
        d->pos_q8.z = d->pos_q8.z < 0 ? -zlim : zlim;
        d->vel_q8.z = -d->vel_q8.z * 3 / 5;
        d->omega_q10.x = -d->omega_q10.x * 7 / 8;
    }

    d->vel_q8.x = d->vel_q8.x * 255 / 256;
    d->vel_q8.z = d->vel_q8.z * 255 / 256;
    d->omega_q10.x = d->omega_q10.x * 254 / 256;
    d->omega_q10.y = d->omega_q10.y * 254 / 256;
    d->omega_q10.z = d->omega_q10.z * 254 / 256;

    int32_t linear = iabs32(d->vel_q8.x) + iabs32(d->vel_q8.y) + iabs32(d->vel_q8.z);
    int32_t angular = iabs32(d->omega_q10.x) + iabs32(d->omega_q10.y) + iabs32(d->omega_q10.z);
    if (min_y <= (1 << 8) && linear < 28 && angular < 520) {
        if (++d->sleep_ticks > 28) {
            d->sleeping = true;
            d->vel_q8 = (t3_vec3_t){0, 0, 0};
            d->omega_q10 = (t3_vec3_t){0, 0, 0};
        }
    } else {
        d->sleep_ticks = 0;
    }

    return impact_out;
}

uint16_t rigid_die_pair_step(rigid_die_t *a, rigid_die_t *b)
{
    if (!a || !b || (a->sleeping && b->sleeping)) return 0;

    int32_t dx = b->pos_q8.x - a->pos_q8.x;
    int32_t dy = b->pos_q8.y - a->pos_q8.y;
    int32_t dz = b->pos_q8.z - a->pos_q8.z;
    int32_t adx = iabs32(dx);
    int32_t ady = iabs32(dy);
    int32_t adz = iabs32(dz);
    const int32_t reach = 23 << 8;
    if (adx >= reach || ady >= reach || adz >= reach) return 0;

    int axis = 0;
    int32_t pen = reach - adx;
    if (reach - ady < pen) {
        axis = 1;
        pen = reach - ady;
    }
    if (reach - adz < pen) {
        axis = 2;
        pen = reach - adz;
    }
    if (pen <= 0) return 0;

    int sign = 1;
    int32_t rel = 0;
    if (axis == 0) {
        sign = dx >= 0 ? 1 : -1;
        rel = (b->vel_q8.x - a->vel_q8.x) * sign;
    } else if (axis == 1) {
        sign = dy >= 0 ? 1 : -1;
        rel = (b->vel_q8.y - a->vel_q8.y) * sign;
    } else {
        sign = dz >= 0 ? 1 : -1;
        rel = (b->vel_q8.z - a->vel_q8.z) * sign;
    }

    int32_t correction = pen / 2 + 1;
    if (axis == 0) {
        a->pos_q8.x -= correction * sign;
        b->pos_q8.x += correction * sign;
    } else if (axis == 1) {
        a->pos_q8.y -= correction * sign;
        b->pos_q8.y += correction * sign;
    } else {
        a->pos_q8.z -= correction * sign;
        b->pos_q8.z += correction * sign;
    }

    uint16_t impact = 0;
    if (rel < 0) {
        int32_t j = (-rel * 3) / 4 + 8;
        impact = (uint16_t)(j > 65535 ? 65535 : j);
        if (axis == 0) {
            a->vel_q8.x -= j * sign;
            b->vel_q8.x += j * sign;
        } else if (axis == 1) {
            a->vel_q8.y -= j * sign;
            b->vel_q8.y += j * sign;
        } else {
            a->vel_q8.z -= j * sign;
            b->vel_q8.z += j * sign;
        }

        a->omega_q10.x += (dz >> 5) * sign;
        a->omega_q10.z -= (dx >> 5) * sign;
        b->omega_q10.x -= (dz >> 5) * sign;
        b->omega_q10.z += (dx >> 5) * sign;
        a->sleeping = false;
        b->sleeping = false;
        a->sleep_ticks = 0;
        b->sleep_ticks = 0;
    }

    return impact;
}

uint8_t rigid_die_top_face(const rigid_die_t *d)
{
    if (!d) return 1;

    static const t3_vec3_t normals[6] = {
        {0, T3_Q14_ONE, 0},
        {0, -T3_Q14_ONE, 0},
        {0, 0, -T3_Q14_ONE},
        {0, 0, T3_Q14_ONE},
        {T3_Q14_ONE, 0, 0},
        {-T3_Q14_ONE, 0, 0},
    };
    static const uint8_t values[6] = {1, 6, 2, 5, 3, 4};

    int32_t best = INT32_MIN;
    uint8_t value = 1;
    for (int i = 0; i < 6; ++i) {
        t3_vec3_t n = t3_quat_rotate_q14(d->orientation, normals[i]);
        if (n.y > best) {
            best = n.y;
            value = values[i];
        }
    }
    return value;
}
