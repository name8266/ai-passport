#pragma once

#include <stdint.h>

typedef struct {
    int16_t x_q10;
    int16_t y_q10;
    int16_t z_q10;
} light_vec3_t;

typedef struct {
    uint8_t ambient;
    uint8_t diffuse;
    uint8_t specular;
    uint8_t shadow;
} light_sample_t;

/* World-space area-light approximation:
 * light comes from upper-left/front at ~45 degrees.
 */
light_vec3_t light_default_direction(void);
light_sample_t light_sample_surface(light_vec3_t normal_q10, uint8_t gloss);
uint32_t light_shade_rgb(uint32_t base_rgb, light_sample_t sample, uint8_t metallic);

/* Cheap dynamic helpers for 2.5D objects. */
light_sample_t light_sample_spinner(int32_t angle_tenths, uint8_t gloss);
uint8_t light_shadow_opacity(uint16_t height_px);
