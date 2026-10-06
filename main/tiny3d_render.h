#pragma once
#include <stdint.h>
#include "rigid_body.h"

#define T3D_DICE_CANVAS_W 176
#define T3D_DICE_CANVAS_H 104

typedef struct {
    uint16_t *pixels;
    int16_t width;
    int16_t height;
    int16_t stride;
} t3d_surface_t;

void t3d_surface_clear(t3d_surface_t *surface, uint32_t rgb888);
void t3d_render_dice_scene(t3d_surface_t *surface, const rigid_die_t dice[2]);
