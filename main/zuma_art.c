#include "zuma_art.h"

/* EMBED_FILES data resides in Flash and is never copied into a full-screen RAM buffer. */
extern const uint8_t board_data[] asm("_binary_board_rgb565_start");
extern const uint8_t guardian_data[] asm("_binary_guardian_rgb565a8_start");
extern const uint8_t marble0_data[] asm("_binary_marble_0_rgb565a8_start");
extern const uint8_t marble1_data[] asm("_binary_marble_1_rgb565a8_start");
extern const uint8_t marble2_data[] asm("_binary_marble_2_rgb565a8_start");
extern const uint8_t marble3_data[] asm("_binary_marble_3_rgb565a8_start");

#define IMAGE_DESC(format, width, height, row_stride, bytes, ptr) \
    { .header = { .cf = format, .w = width, .h = height, .stride = row_stride }, \
      .data_size = bytes, .data = ptr }

const lv_image_dsc_t zuma_board_image =
    IMAGE_DESC(LV_COLOR_FORMAT_RGB565, 240, 320, 480, 153600, board_data);
const lv_image_dsc_t zuma_guardian_image =
    IMAGE_DESC(LV_COLOR_FORMAT_RGB565A8, 72, 72, 144, 15552, guardian_data);
const lv_image_dsc_t zuma_marble_images[4] = {
    IMAGE_DESC(LV_COLOR_FORMAT_RGB565A8, 14, 14, 28, 588, marble0_data),
    IMAGE_DESC(LV_COLOR_FORMAT_RGB565A8, 14, 14, 28, 588, marble1_data),
    IMAGE_DESC(LV_COLOR_FORMAT_RGB565A8, 14, 14, 28, 588, marble2_data),
    IMAGE_DESC(LV_COLOR_FORMAT_RGB565A8, 14, 14, 28, 588, marble3_data),
};
