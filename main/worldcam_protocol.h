#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define WC_WIDTH 192
#define WC_HEIGHT 128
#define WC_FULL_WIDTH 240
#define WC_FULL_HEIGHT 160
#define WC_PIXELS_BYTES (WC_WIDTH * WC_HEIGHT * 2)
#define WC_MAX_PIXELS_BYTES (WC_FULL_WIDTH * WC_FULL_HEIGHT * 2)
#define WC_HEADER_BYTES 12
#define WC_INDEX_HEADER_BYTES 8
#define WC_INDEX_RECORD_BYTES 8
#define WC_LOCATION_LIMIT 2048
#define WC_MAP_WIDTH 216
#define WC_MAP_HEIGHT 128
#define WC_FLAG_AVAILABLE 1
#define WC_FLAG_CAPITAL 2
#define WC_FLAG_HAS_SOURCE 4
typedef struct { uint16_t index; int16_t lat,lon; uint8_t flags; } wc_point_t;
bool wc_frame_header(const uint8_t *header, size_t size, uint32_t *acquired);
bool wc_frame_info(const uint8_t *header, size_t size, uint16_t *width, uint16_t *height, uint32_t *acquired);
bool wc_index_header(const uint8_t *header, size_t size, size_t *count);
bool wc_index_point(const uint8_t *record, size_t size, wc_point_t *point);
void wc_map_project(int16_t lat,int16_t lon,int *x,int *y);
size_t wc_wrap_index(size_t index, int direction, size_t count);
bool wc_random_index(const wc_point_t *points,size_t count,uint32_t random,size_t current,size_t *chosen);
bool wc_gateway_valid(const char *url);
bool wc_utf8_valid(const char *text);
