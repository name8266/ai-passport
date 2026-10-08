#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include "../firmware/main/nav_packet.h"

static void sign_packet(uint8_t *p) {
    p[19] = 0;
    for (int i=0; i<19; i++) p[19] ^= p[i];
}

int main(void) {
    uint8_t p[20]={0xA5,1,NAV_RIGHT,NAV_FLAG_ACTIVE|NAV_FLAG_GPS,
        0x5E,0x01,0xE0,0x01,0xEC,0x04,0x4C,0x04,18,42,0x33,0,90,0,0,0};
    sign_packet(p);
    nav_packet_t out={0};
    assert(nav_packet_decode(p,20,&out));
    assert(out.turn_meters==350 && out.speed_tenths_kmh==480);
    assert(out.remaining_tens_meters==1260 && out.remaining_seconds==1100);
    p[4]^=1; assert(!nav_packet_decode(p,20,&out));
    p[4]^=1; p[12]=25; sign_packet(p);
    assert(!nav_packet_decode(p,20,&out));
    p[12]=18; sign_packet(p);
    assert(!nav_packet_decode(p,19,&out));
    assert(!nav_packet_decode(NULL,20,&out));
    puts("nav packet tests passed");
    return 0;
}
