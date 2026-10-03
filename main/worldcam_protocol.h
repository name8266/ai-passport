#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "worldcam_catalog.h"

#define WC_WIDTH 192
#define WC_HEIGHT 128
#define WC_FULL_WIDTH 240
#define WC_FULL_HEIGHT 160
#define WC_MAP_WIDTH 216
#define WC_MAP_HEIGHT 128

void wc_map_project(int16_t lat, int16_t lon, int *x, int *y);
size_t wc_wrap_index(size_t index, int direction, size_t count);
bool wc_random_index(const wc_location_t *points, size_t count, uint32_t random, size_t current, size_t *chosen);
bool wc_resolve_usap(const char *metadata, size_t length, char *out, size_t out_size);
bool wc_utf8_valid(const char *text);
