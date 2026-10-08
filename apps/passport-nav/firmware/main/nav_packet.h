#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define NAV_PACKET_SIZE 20
#define NAV_MAGIC 0xA5
#define NAV_VERSION 1
#define NAV_FLAG_ACTIVE 0x01
#define NAV_FLAG_GPS 0x02
#define NAV_FLAG_REROUTE 0x04

typedef enum {
    NAV_NONE = 0, NAV_STRAIGHT, NAV_LEFT, NAV_RIGHT,
    NAV_UTURN, NAV_ARRIVE, NAV_BEAR_LEFT, NAV_BEAR_RIGHT
} nav_maneuver_t;

typedef struct {
    uint8_t maneuver, flags;
    uint16_t turn_meters, speed_tenths_kmh, remaining_tens_meters;
    uint16_t remaining_seconds;
    uint8_t eta_hour, eta_minute;
    uint16_t sequence, bearing_deg;
} nav_packet_t;

bool nav_packet_decode(const uint8_t *wire, size_t length, nav_packet_t *out);
