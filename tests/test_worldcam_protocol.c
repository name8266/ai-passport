#include "worldcam_protocol.h"
#include <assert.h>
#include <string.h>

int main(void)
{
    int x = -1, y = -1;
    wc_map_project(0, 0, &x, &y);
    assert(x >= 107 && x <= 108);
    assert(y >= 63 && y <= 64);

    assert(wc_wrap_index(0, -1, 3) == 2);
    assert(wc_wrap_index(2, 1, 3) == 0);

    wc_location_t rows[3] = {
        {.flags = 0},
        {.flags = WC_FLAG_AVAILABLE},
        {.flags = WC_FLAG_AVAILABLE},
    };
    size_t chosen = 99;
    assert(wc_random_index(rows, 3, 0, 1, &chosen));
    assert(chosen == 2);

    char url[192];
    const char meta[] = "SouthPoleImage_20261003.jpg?cache=1,other";
    assert(wc_resolve_usap(meta, strlen(meta), url, sizeof(url)));
    assert(strcmp(url, "https://www.usap.gov/videoClipsAndMaps/SouthPoleWebcam/SouthPoleImage_20261003.jpg") == 0);
    assert(!wc_resolve_usap("../bad.jpg,other", strlen("../bad.jpg,other"), url, sizeof(url)));
    assert(!wc_resolve_usap("bad.png,other", strlen("bad.png,other"), url, sizeof(url)));

    assert(wc_utf8_valid("中国 Beijing"));
    assert(!wc_utf8_valid("\x01"));
    return 0;
}
