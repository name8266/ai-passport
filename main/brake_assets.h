#pragma once
#include <stdint.h>
typedef struct { uint16_t width, height; const uint8_t *pixels; } bh_sprite_t;
typedef struct { uint32_t codepoint; uint8_t width; uint8_t bitmap[28]; } bh_glyph_t;
extern const uint8_t bh_palette[1024];
extern const bh_sprite_t bh_cockpit, bh_horizon, bh_asphalt, bh_wall, bh_cliff,
    bh_truck, bh_construction, bh_glass, bh_logo;
extern const bh_sprite_t bh_guardrail, bh_lamp, bh_tree, bh_mountains;
extern const bh_sprite_t bh_vehicle_body;
extern const uint8_t bh_vehicle_brake_mask[];
extern const bh_glyph_t bh_glyphs[];
extern const unsigned bh_glyph_count;
enum { INK=240, PANEL, MUTED, WHITE, AMBER, RED, TEAL, GREEN, ROAD, GRASS, LINE, STEEL, DARKRED, GOLD, BLACK, SKY };
