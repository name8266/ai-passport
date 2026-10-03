#include "worldcam_protocol.h"
#include <ctype.h>
#include <string.h>

void wc_map_project(int16_t lat, int16_t lon, int *x, int *y)
{
    if (lat > 9000) lat = 9000;
    if (lat < -9000) lat = -9000;
    if (lon > 18000) lon = 18000;
    if (lon < -18000) lon = -18000;
    *x = ((int32_t)lon + 18000) * (WC_MAP_WIDTH - 1) / 36000;
    *y = (9000 - (int32_t)lat) * (WC_MAP_HEIGHT - 1) / 18000;
}

size_t wc_wrap_index(size_t index, int direction, size_t count)
{
    if (!count) return 0;
    index %= count;
    return direction < 0 ? (index ? index - 1 : count - 1) : (index + 1) % count;
}

bool wc_random_index(const wc_location_t *points, size_t count, uint32_t random, size_t current, size_t *chosen)
{
    if (!points || !chosen || !count) return false;
    size_t eligible = 0;
    for (size_t i = 0; i < count; ++i) {
        if ((points[i].flags & WC_FLAG_AVAILABLE) && i != current) ++eligible;
    }
    if (!eligible) {
        if (current < count && (points[current].flags & WC_FLAG_AVAILABLE)) {
            *chosen = current;
            return true;
        }
        return false;
    }
    size_t pick = random % eligible;
    for (size_t i = 0; i < count; ++i) {
        if ((points[i].flags & WC_FLAG_AVAILABLE) && i != current) {
            if (!pick) {
                *chosen = i;
                return true;
            }
            --pick;
        }
    }
    return false;
}

bool wc_resolve_usap(const char *metadata, size_t length, char *out, size_t out_size)
{
    static const char prefix[] = "https://www.usap.gov/videoClipsAndMaps/SouthPoleWebcam/";
    if (!metadata || !length || !out || out_size <= sizeof(prefix)) return false;

    size_t i = 0;
    while (i < length && isspace((unsigned char)metadata[i])) ++i;
    size_t start = i;
    while (i < length && (isalnum((unsigned char)metadata[i]) || metadata[i] == '_' || metadata[i] == '-')) ++i;
    if (i == start || i + 4 > length || memcmp(metadata + i, ".jpg", 4) != 0) return false;
    size_t filename_len = i + 4 - start;
    i += 4;
    if (i < length && metadata[i] == '?') {
        ++i;
        while (i < length && metadata[i] != ',' && metadata[i] != '\r' && metadata[i] != '\n') ++i;
    }
    if (i >= length || metadata[i] != ',') return false;
    if (sizeof(prefix) - 1 + filename_len + 1 > out_size) return false;

    memcpy(out, prefix, sizeof(prefix) - 1);
    memcpy(out + sizeof(prefix) - 1, metadata + start, filename_len);
    out[sizeof(prefix) - 1 + filename_len] = 0;
    return true;
}

bool wc_utf8_valid(const char *s)
{
    if (!s) return false;
    const unsigned char *p = (const unsigned char *)s;
    while (*p) {
        if (*p < 0x80) {
            if (*p < 32) return false;
            ++p;
            continue;
        }
        uint32_t code;
        unsigned n;
        if (*p >= 0xc2 && *p <= 0xdf) { code = *p & 31; n = 1; }
        else if (*p >= 0xe0 && *p <= 0xef) { code = *p & 15; n = 2; }
        else if (*p >= 0xf0 && *p <= 0xf4) { code = *p & 7; n = 3; }
        else return false;
        ++p;
        for (unsigned i = 0; i < n; ++i) {
            if ((*p & 0xc0) != 0x80) return false;
            code = (code << 6) | (*p++ & 63);
        }
        if ((n == 2 && code < 0x800) || (n == 3 && code < 0x10000) ||
            code > 0x10ffff || (code >= 0xd800 && code <= 0xdfff)) return false;
    }
    return true;
}
