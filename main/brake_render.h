#pragma once
#include "brake_hero.h"
#include "brake_scores.h"
#define BH_W 240
#define BH_H 320
#define BH_PALETTE_BYTES 1024
#define BH_FRAME_BYTES (BH_PALETTE_BYTES + BH_W * BH_H)
typedef enum { BH_HOME, BH_COUNTDOWN, BH_DRIVE, BH_CRASH, BH_RESULT, BH_BOARD, BH_HELP } bh_page_t;
typedef struct {
    bh_page_t page;
    bh_state_t car;
    bh_scores_t scores;
    int selection, countdown, battery, rank, board_page;
    unsigned animation;
    bool audio_ok, storage_ok, buttons_ok, saved;
} bh_view_t;
/* Indexed framebuffer includes LVGL-compatible BGRA palette; one 77,824 byte
 * buffer, no full-size source image decoding or dynamic allocation. */
void bh_render(uint8_t frame[BH_FRAME_BYTES], const bh_view_t *v);
bool bh_font_has(uint32_t codepoint);
