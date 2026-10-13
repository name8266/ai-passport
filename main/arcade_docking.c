#include "arcade_docking.h"
#include <math.h>

/* Six rigid-body control axes. All translations are in metres and metres/s. */
static float limitf(float v, float lo, float hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}
static float magnitude(float v) { return v < 0 ? -v : v; }
static const char *const AXES[6] = {"Z", "X", "Y", "YAW", "PITCH", "ROLL"};
const char *arcade_docking_axis_name(int axis) {
    return axis >= 0 && axis < 6 ? AXES[axis] : "?";
}
void arcade_docking_start(arcade_game_t *g) {
    g->lives = 1;
    g->dock_x = 1.65f; g->dock_y = -1.20f;
    g->dock_z = 20.0f; g->dock_vz = -0.18f;
    g->dock_yaw = 0.10f; g->dock_pitch = -0.09f;
    g->dock_roll = 0.07f; g->dock_axis = 0;
    g->dock_propellant = 100.0f;
}
void arcade_docking_input(arcade_game_t *g, arcade_action_t action) {
    if (action == ARCADE_LEFT_UP && g->held == -1) g->held = 0;
    if (action == ARCADE_RIGHT_UP && g->held == 1) g->held = 0;
    if (action == ARCADE_LEFT_DOWN) g->held = -1;
    if (action == ARCADE_RIGHT_DOWN) g->held = 1;
    if (action == ARCADE_OK_CLICK) {
        g->held = 0;
        g->dock_axis = (g->dock_axis + 1) % 6;
    }
}
void arcade_docking_demo(arcade_game_t *g, float dt) {
    /* An attract-mode autopilot: move visibly without simulating key chords. */
    (void)dt;
    g->held = 0;
    g->dock_vz = -0.60f;
    g->dock_vx = limitf(-g->dock_x * 0.22f, -0.50f, 0.50f);
    g->dock_vy = limitf(-g->dock_y * 0.22f, -0.50f, 0.50f);
    g->dock_wyaw = -g->dock_yaw * 0.32f;
    g->dock_wpitch = -g->dock_pitch * 0.32f;
    g->dock_wroll = -g->dock_roll * 0.32f;
}
void arcade_docking_tick(arcade_game_t *g, float dt) {
    if (g->held && g->dock_propellant > 0.0f) {
        float pulse = (float)g->held * dt;
        switch (g->dock_axis) {
        case 0: g->dock_vz += pulse * 0.60f; break;
        case 1: g->dock_vx += pulse * 0.36f; break;
        case 2: g->dock_vy += pulse * 0.36f; break;
        case 3: g->dock_wyaw += pulse * 0.11f; break;
        case 4: g->dock_wpitch += pulse * 0.11f; break;
        case 5: g->dock_wroll += pulse * 0.11f; break;
        default: break;
        }
        g->dock_propellant = limitf(g->dock_propellant - dt * 0.65f, 0.0f, 100.0f);
    }
    /* No artificial drag: a counter-thrust is necessary to stop the craft. */
    g->dock_vx = limitf(g->dock_vx, -3.0f, 3.0f);
    g->dock_vy = limitf(g->dock_vy, -3.0f, 3.0f);
    g->dock_vz = limitf(g->dock_vz, -3.0f, 3.0f);
    g->dock_wyaw = limitf(g->dock_wyaw, -0.6f, 0.6f);
    g->dock_wpitch = limitf(g->dock_wpitch, -0.6f, 0.6f);
    g->dock_wroll = limitf(g->dock_wroll, -0.6f, 0.6f);
    g->dock_x += g->dock_vx * dt;
    g->dock_y += g->dock_vy * dt;
    g->dock_z += g->dock_vz * dt;
    g->dock_yaw += g->dock_wyaw * dt;
    g->dock_pitch += g->dock_wpitch * dt;
    g->dock_roll += g->dock_wroll * dt;
    if (g->dock_z <= 0.5f) {
        bool aligned = magnitude(g->dock_x) < 1.1f &&
                       magnitude(g->dock_y) < 1.1f &&
                       magnitude(g->dock_yaw) < 0.18f &&
                       magnitude(g->dock_pitch) < 0.18f &&
                       magnitude(g->dock_roll) < 0.18f;
        bool slow = magnitude(g->dock_vx) < 0.40f &&
                    magnitude(g->dock_vy) < 0.40f &&
                    magnitude(g->dock_vz) < 0.65f &&
                    magnitude(g->dock_wyaw) < 0.14f &&
                    magnitude(g->dock_wpitch) < 0.14f &&
                    magnitude(g->dock_wroll) < 0.14f;
        g->mode = aligned && slow ? ARCADE_WON : ARCADE_LOST;
        if (g->mode == ARCADE_WON)
            g->score = 1000u + (uint32_t)(g->dock_propellant * 5.0f) +
                       (uint32_t)(limitf(130.0f - g->elapsed, 0.0f, 130.0f) * 5.0f);
        g->held = 0;
    } else if (g->dock_z > 60.0f ||
               magnitude(g->dock_x) > 16.0f || magnitude(g->dock_y) > 16.0f ||
               g->elapsed > 130.0f) {
        g->mode = ARCADE_LOST;
        g->held = 0;
    }
}
