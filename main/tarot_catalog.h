#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "lvgl.h"

#define TAROT_IMAGE_WIDTH 112
#define TAROT_IMAGE_HEIGHT 192
#define TAROT_IMAGE_BYTES (TAROT_IMAGE_WIDTH * TAROT_IMAGE_HEIGHT * 2)

const char *tarot_card_name(uint8_t card_id);
const char *tarot_card_keyword(uint8_t card_id, bool reversed);
void tarot_card_interpret(uint8_t card_id, bool reversed, const char *position,
                          char *buffer, size_t buffer_size);
bool tarot_card_image(uint8_t card_id, lv_image_dsc_t *descriptor);
