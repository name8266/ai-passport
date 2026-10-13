#pragma once

#include <stdbool.h>
#include <stdint.h>

#define ZUMA_MAX_BALLS 64
#define ZUMA_PATH_POINTS 512

typedef enum {
    ZUMA_MENU,
    ZUMA_PLAYING,
    ZUMA_PAUSED,
    ZUMA_WON,
    ZUMA_LOST,
} zuma_mode_t;

typedef enum {
    ZUMA_SOUND_NONE,
    ZUMA_SOUND_SHOOT,
    ZUMA_SOUND_HIT,
    ZUMA_SOUND_MATCH,
    ZUMA_SOUND_COMBO,
    ZUMA_SOUND_SWAP,
    ZUMA_SOUND_WIN,
    ZUMA_SOUND_LOSE,
} zuma_sound_t;

typedef struct { float x, y, distance; } zuma_path_point_t;
typedef struct { float x, y, vx, vy; uint8_t color; bool active; } zuma_shot_t;

typedef struct {
    zuma_mode_t mode;
    uint8_t difficulty;
    uint8_t balls[ZUMA_MAX_BALLS];
    float ball_distance[ZUMA_MAX_BALLS];
    uint8_t count;
    uint8_t spawned;
    uint8_t quota;
    uint8_t current_color;
    uint8_t next_color;
    float lead;
    float spawn_clock;
    float aim_radians;
    float rollback;
    zuma_shot_t shot;
    zuma_path_point_t path[ZUMA_PATH_POINTS];
    uint16_t path_count;
    float path_length;
    uint32_t score;
    uint32_t seed;
    uint8_t combo;
    zuma_sound_t sound;
} zuma_game_t;

void zuma_init(zuma_game_t *g, uint32_t seed);
void zuma_start(zuma_game_t *g);
void zuma_tick(zuma_game_t *g, float dt);
void zuma_turn(zuma_game_t *g, int direction);
void zuma_fire(zuma_game_t *g);
void zuma_swap(zuma_game_t *g);
void zuma_toggle_pause(zuma_game_t *g);
void zuma_to_menu(zuma_game_t *g);
void zuma_path_position(const zuma_game_t *g, float distance, float *x, float *y);
zuma_sound_t zuma_take_sound(zuma_game_t *g);
