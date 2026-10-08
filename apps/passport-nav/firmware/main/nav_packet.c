#include "nav_packet.h"

static uint16_t u16(const uint8_t *p) {
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

bool nav_packet_decode(const uint8_t *w, size_t n, nav_packet_t *out) {
    if (!w || !out || n != NAV_PACKET_SIZE || w[0] != NAV_MAGIC ||
        w[1] != NAV_VERSION || w[2] > NAV_BEAR_RIGHT ||
        (w[3] & ~7u) || w[12] > 23 || w[13] > 59 ||
        u16(w + 16) > 359 || w[18] != 0) return false;
    uint8_t check = 0;
    for (size_t i = 0; i < 19; i++) check ^= w[i];
    if (check != w[19]) return false;
    *out = (nav_packet_t){
        .maneuver=w[2], .flags=w[3], .turn_meters=u16(w+4),
        .speed_tenths_kmh=u16(w+6), .remaining_tens_meters=u16(w+8),
        .remaining_seconds=u16(w+10), .eta_hour=w[12], .eta_minute=w[13],
        .sequence=u16(w+14), .bearing_deg=u16(w+16)
    };
    return true;
}
