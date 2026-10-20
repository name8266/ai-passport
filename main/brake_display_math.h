#pragma once
#include "brake_render.h"
/* Prepare ST7789 wire-order RGB565. Source BGRA palette remains in Flash. */
void bh_rgb565_palette(uint16_t out[256], const uint8_t bgra[1024]);
void bh_convert_rows(uint16_t *out, const uint8_t frame[BH_FRAME_BYTES],
                     unsigned first_row, unsigned rows, const uint16_t palette[256]);
