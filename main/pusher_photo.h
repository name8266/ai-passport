#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
/* Bounded LZ4 block decode. Failure may modify destination inside its bounds. */
bool cp_photo_decode(const uint8_t *source,size_t source_size,uint8_t *output,size_t output_size);
