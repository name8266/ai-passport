#include "brake_display_math.h"
void bh_rgb565_palette(uint16_t out[256],const uint8_t bgra[1024]) {
    for(unsigned i=0;i<256;++i) {
        unsigned b=bgra[i*4],g=bgra[i*4+1],r=bgra[i*4+2];
        uint16_t c=(uint16_t)(((r&248)<<8)|((g&252)<<3)|(b>>3));
        out[i]=(uint16_t)((c>>8)|(c<<8));
    }
}
void bh_convert_rows(uint16_t *out,const uint8_t frame[BH_FRAME_BYTES],
                     unsigned first,unsigned rows,const uint16_t palette[256]) {
    if(first>=BH_H)return;
    if(rows>BH_H-first)rows=BH_H-first;
    const uint8_t *p=frame+BH_PALETTE_BYTES+first*BH_W;
    for(unsigned i=0;i<rows*BH_W;++i)out[i]=palette[p[i]];
}
