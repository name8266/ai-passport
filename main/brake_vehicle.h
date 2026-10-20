#pragma once
#include "brake_render.h"

/* Visual game state. The centered prerendered V800 body and animated canopy
 * share one coordinate system; these are game visuals, not crash predictions. */
typedef struct {
    float tent_opening, roof_lift, crush, pitch, fall;
    bool brake_lights;
} bh_vehicle_pose_t;
bh_vehicle_pose_t bh_vehicle_pose(const bh_view_t *view);
void bh_vehicle_draw(uint8_t *pixels, const bh_view_t *view);
