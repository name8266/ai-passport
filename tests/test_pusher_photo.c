#include "pusher_photo.h"
#include <assert.h>
#include <string.h>
int main(void){
    unsigned char out[258];
    const unsigned char repeated[]={0x26,'A','B',2,0,0};
    for(unsigned align=0;align<4;++align){memset(out,0xa5,sizeof(out));
        assert(cp_photo_decode(repeated,sizeof(repeated),out+4+align,12));
        for(unsigned i=0;i<12;++i)assert(out[4+align+i]==(i&1?'B':'A'));
        assert(out[3+align]==0xa5&&out[16+align]==0xa5);
    }
    const unsigned char zero[]={0x10,'A',0,0};assert(!cp_photo_decode(zero,sizeof(zero),out,32));
    const unsigned char before[]={0,1,0};assert(!cp_photo_decode(before,sizeof(before),out,32));
    const unsigned char short_literal[]={0x30,'A'};assert(!cp_photo_decode(short_literal,sizeof(short_literal),out,32));
    const unsigned char extension[]={0xf0,255};assert(!cp_photo_decode(extension,sizeof(extension),out,32));
    unsigned seed=8266;
    for(unsigned trial=0;trial<10000;++trial){unsigned char input[64];unsigned n=trial%64;
        for(unsigned i=0;i<n;++i){seed=seed*1664525+1013904223;input[i]=(unsigned char)(seed>>24);}
        memset(out,0xa5,sizeof(out));(void)cp_photo_decode(input,n,out+4,128);
        for(unsigned i=0;i<4;++i)assert(out[i]==0xa5);
        for(unsigned i=132;i<sizeof(out);++i)assert(out[i]==0xa5);
    }
}
