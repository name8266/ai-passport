#pragma once
#include "brake_render.h"

/* Visual game state only. This mesh is an original low-polygon interpretation
 * of the V800 silhouette, not manufacturer CAD or a crash prediction. */
typedef struct {
    float tent_opening, roof_lift, crush, pitch, fall;
    bool brake_lights;
} bh_vehicle_pose_t;
bh_vehicle_pose_t bh_vehicle_pose(const bh_view_t *view);
void bh_vehicle_draw(uint8_t *pixels, const bh_view_t *view);
