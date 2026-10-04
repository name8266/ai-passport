#pragma once
#include <stddef.h>
#include <stdint.h>

#define WC_CAMERA_COUNT 825
#define WC_LOCATION_COUNT 1020
#define WC_CAMERA_NONE UINT16_MAX

#define WC_FLAG_AVAILABLE 1u
#define WC_FLAG_CAPITAL 2u
#define WC_FLAG_HAS_SOURCE 4u

#define WC_RESOLVER_NONE 0u
#define WC_RESOLVER_USAP 1u

typedef struct {
    const char *snapshot;
    uint8_t resolver;
} wc_camera_t;

typedef struct {
    int16_t lat;
    int16_t lon;
    uint16_t camera;
    uint8_t flags;
    const char *name;
    const char *country;
} wc_location_t;

extern const wc_camera_t wc_cameras[WC_CAMERA_COUNT];
extern const wc_location_t wc_locations[WC_LOCATION_COUNT];
