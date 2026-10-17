#pragma once
#include "pusher_physics.h"
#include "pusher_records.h"
#define CP_SCREEN_W 240
#define CP_SCREEN_H 320
#define CP_TILE_H 32
/* RGB565 in native host byte order. Prepare once; draw ten 32-line tiles.
 * A per-pixel depth buffer, not sprite ordering, resolves towers and tilted rims. */
bool cp_render_assets_ready(void);
void cp_render_project_mm(int x,int y,int z,int *sx,int *sy);
void cp_render_prepare(const cp_game_t *g);
void cp_render_tile(const cp_game_t *g,int y0,uint16_t *rgb,uint16_t *depth,int battery);

void cp_render_background_tile(int y0,uint16_t *rgb,uint16_t *depth);

/* Old/new projected bounds and cabinet/HUD changes, one bit per tile. */
uint16_t cp_render_dirty(void);

bool cp_render_base_tile(int y0,uint16_t *rgb,uint16_t *depth);

/* Panel byte order; avoids a second full-tile byte-swap pass. */
void cp_render_spi_tile(const cp_game_t *g,int y0,uint16_t *rgb,uint16_t *depth,int battery);

typedef struct {uint32_t base_us,faces_us,expand_us,hud_us,scene_us,coins_us;} cp_render_profile_t;
cp_render_profile_t cp_render_profile(void);

/* Even, exclusive native x bounds; returned buffer retains a 240-pixel stride. */
void cp_render_region(int y0,int *x0,int *x1);
void cp_render_spi_region(const cp_game_t *g,int y0,uint16_t *rgb,uint16_t *depth,int battery,int x0,int x1);

void cp_render_pack_region(uint16_t *rgb,int x0,int x1);

#ifdef CP_RENDER_HOST
/* Offline exact-geometry bake, centered 20x20 RGB/depth output. */
void cp_render_bake_coin(cp_q4 pose,uint16_t *rgb,int16_t *relative_depth);
unsigned cp_render_normal_probe(int x,int y,int z);
#endif

void cp_render_records(const cp_records_t *records);

/* Exact dirty scanlines within the selected 32-row tile; y1 is exclusive. */
void cp_render_area(int tile_y,int *x0,int *y0,int *x1,int *y1);
void cp_render_spi_area(const cp_game_t *g,int y0,int height,uint16_t *rgb,uint16_t *depth,int battery,int x0,int x1);
void cp_render_pack_area(uint16_t *rgb,int x0,int x1,unsigned height);
