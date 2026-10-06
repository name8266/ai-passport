#include "light_core.h"

static int32_t clamp_i32(int32_t v, int32_t lo, int32_t hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static uint8_t clamp_u8(int32_t v)
{
    return (uint8_t)clamp_i32(v, 0, 255);
}

light_vec3_t light_default_direction(void)
{
    /* (-0.5, -0.707, +0.5) in Q10:
     * upper-left in screen space, elevated ~45 degrees toward viewer.
     */
    light_vec3_t v = { -512, -724, 512 };
    return v;
}

light_sample_t light_sample_surface(light_vec3_t n, uint8_t gloss)
{
    const light_vec3_t l = light_default_direction();
    /* Half vector of L and V=(0,0,1), normalized approximately. */
    const light_vec3_t h = { -288, -408, 894 };

    int32_t ndotl = ((int32_t)n.x_q10 * l.x_q10 +
                     (int32_t)n.y_q10 * l.y_q10 +
                     (int32_t)n.z_q10 * l.z_q10) >> 10;
    int32_t ndoth = ((int32_t)n.x_q10 * h.x_q10 +
                     (int32_t)n.y_q10 * h.y_q10 +
                     (int32_t)n.z_q10 * h.z_q10) >> 10;

    if (ndotl < 0) ndotl = 0;
    if (ndoth < 0) ndoth = 0;
    if (ndotl > 1024) ndotl = 1024;
    if (ndoth > 1024) ndoth = 1024;

    int32_t spec2 = (ndoth * ndoth) >> 10;
    int32_t spec4 = (spec2 * spec2) >> 10;
    int32_t gloss_gain = 64 + ((int32_t)gloss * 191 / 255);

    light_sample_t s = {
        .ambient = 46,
        .diffuse = clamp_u8((ndotl * 176) >> 10),
        .specular = clamp_u8((spec4 * gloss_gain) >> 10),
        .shadow = clamp_u8(210 - ((ndotl * 150) >> 10)),
    };
    return s;
}

uint32_t light_shade_rgb(uint32_t base, light_sample_t s, uint8_t metallic)
{
    int32_t r = (base >> 16) & 0xFF;
    int32_t g = (base >> 8) & 0xFF;
    int32_t b = base & 0xFF;

    int32_t energy = s.ambient + s.diffuse;
    if (energy > 255) energy = 255;

    r = r * energy / 220;
    g = g * energy / 220;
    b = b * energy / 220;

    int32_t white = s.specular * (255 - metallic) / 255;
    int32_t metal = s.specular * metallic / 255;
    r += white + metal * ((base >> 16) & 0xFF) / 255;
    g += white + metal * ((base >> 8) & 0xFF) / 255;
    b += white + metal * (base & 0xFF) / 255;

    r = clamp_i32(r, 0, 255);
    g = clamp_i32(g, 0, 255);
    b = clamp_i32(b, 0, 255);
    return ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}

light_sample_t light_sample_spinner(int32_t angle_tenths, uint8_t gloss)
{
    /* 16-step unit-circle approximation. The normal is tilted toward viewer
     * so a rotating 2.5D face gains/losses light without sinf/cosf.
     */
    static const int16_t cos16[16] = {
        1024, 946, 724, 392, 0, -392, -724, -946,
        -1024, -946, -724, -392, 0, 392, 724, 946
    };
    static const int16_t sin16[16] = {
        0, 392, 724, 946, 1024, 946, 724, 392,
        0, -392, -724, -946, -1024, -946, -724, -392
    };

    int32_t wrapped = angle_tenths % 3600;
    if (wrapped < 0) wrapped += 3600;
    int idx = (int)((wrapped + 112) / 225) & 15;

    light_vec3_t n = {
        .x_q10 = (int16_t)(cos16[idx] * 3 / 5),
        .y_q10 = (int16_t)(sin16[idx] * 2 / 5),
        .z_q10 = 724,
    };
    return light_sample_surface(n, gloss);
}

uint8_t light_shadow_opacity(uint16_t height_px)
{
    /* Object closer to the "table" casts a stronger, tighter shadow. */
    int32_t opa = 150 - (int32_t)height_px * 3;
    return clamp_u8(opa);
}
