#pragma once
#include <stdint.h>
#include "rigid_body.h"

#define T3D_DICE_CANVAS_W 200
#define T3D_DICE_CANVAS_H 168

typedef struct {
    uint16_t *pixels;
    int16_t width;
    int16_t height;
    int16_t stride;
} t3d_surface_t;

void t3d_surface_clear(t3d_surface_t *surface, uint32_t rgb888);
void t3d_render_dice_scene(t3d_surface_t *surface, const rigid_die_t dice[2]);

void t3d_render_dice_count(t3d_surface_t *surface, const rigid_die_t *dice, uint8_t count);
void t3d_render_roulette(t3d_surface_t *surface, int32_t wheel_angle, int32_t ball_angle, int ball_radius);
uint8_t t3d_roulette_number(uint8_t pocket);

bool t3d_cube_mesh_valid(void);
