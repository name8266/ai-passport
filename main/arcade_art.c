#include "arcade_art.h"

/* Embedded RGB565 assets stay in Flash. The LVGL draw buffer remains small. */
extern const uint8_t breakout_data[] asm("_binary_breakout_rgb565_start");
extern const uint8_t tetris_data[] asm("_binary_tetris_rgb565_start");
extern const uint8_t doodle_data[] asm("_binary_doodle_rgb565_start");
extern const uint8_t snake_data[] asm("_binary_snake_rgb565_start");
extern const uint8_t shooter_data[] asm("_binary_shooter_rgb565_start");
extern const uint8_t flappy_data[] asm("_binary_flappy_rgb565_start");
extern const uint8_t lander_data[] asm("_binary_lander_rgb565_start");
extern const uint8_t racer_data[] asm("_binary_racer_rgb565_start");
extern const uint8_t docking_data[] asm("_binary_docking_rgb565_start");
extern const uint8_t breakout_sprite_data[] asm("_binary_breakout_sprite_rgb565a8_start");
extern const uint8_t tetris_sprite_data[] asm("_binary_tetris_sprite_rgb565a8_start");
extern const uint8_t doodle_sprite_data[] asm("_binary_doodle_sprite_rgb565a8_start");
extern const uint8_t snake_sprite_data[] asm("_binary_snake_sprite_rgb565a8_start");
extern const uint8_t shooter_sprite_data[] asm("_binary_shooter_sprite_rgb565a8_start");
extern const uint8_t flappy_sprite_data[] asm("_binary_flappy_sprite_rgb565a8_start");
extern const uint8_t lander_sprite_data[] asm("_binary_lander_sprite_rgb565a8_start");
extern const uint8_t racer_sprite_data[] asm("_binary_racer_sprite_rgb565a8_start");
extern const uint8_t docking_sprite_data[] asm("_binary_docking_sprite_rgb565a8_start");
extern const uint8_t docking_ship_data[] asm("_binary_docking_ship_rgb565a8_start");

#define BG(ptr) { .header = { .cf = LV_COLOR_FORMAT_RGB565, .w = 240, .h = 320, .stride = 480 }, \
                  .data_size = 153600, .data = ptr }
const lv_image_dsc_t arcade_backgrounds[ARCADE_GAME_COUNT] = {
    BG(breakout_data), BG(tetris_data), BG(doodle_data), BG(snake_data),
    BG(shooter_data), BG(flappy_data), BG(lander_data), BG(racer_data), BG(docking_data)
};
#define SPRITE(width, height, ptr) { .header = { .cf = LV_COLOR_FORMAT_RGB565A8, \
    .w = width, .h = height, .stride = (width) * 2 }, \
    .data_size = (width) * (height) * 3, .data = ptr }
const lv_image_dsc_t arcade_sprites[ARCADE_GAME_COUNT] = {
    SPRITE(60, 18, breakout_sprite_data), SPRITE(15, 13, tetris_sprite_data),
    SPRITE(22, 26, doodle_sprite_data), SPRITE(18, 18, snake_sprite_data),
    SPRITE(30, 29, shooter_sprite_data), SPRITE(24, 20, flappy_sprite_data),
    SPRITE(24, 24, lander_sprite_data), SPRITE(28, 36, racer_sprite_data),
    SPRITE(48, 48, docking_sprite_data)
};

const lv_image_dsc_t arcade_docking_ship = SPRITE(24, 20, docking_ship_data);
