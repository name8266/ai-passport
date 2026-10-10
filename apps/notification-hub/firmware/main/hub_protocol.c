#include "hub_protocol.h"
#include <string.h>

static uint16_t read16(const uint8_t *p) {
    return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}
static uint32_t read32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

bool hub_event_parse(const uint8_t *p, size_t n, hub_event_t *out) {
    if (!p || !out || n != 8 || p[0] > 2 || p[2] > 11) return false;
    *out = (hub_event_t){.event=p[0], .flags=p[1],
        .category=p[2], .count=p[3], .uid=read32(p+4)};
    return true;
}

void hub_decoder_begin(hub_decoder_t *p, uint32_t uid) {
    if (!p) return;
    memset(p, 0, sizeof(*p));
    p->expected_uid = uid;
}

void hub_utf8_copy(char *dst,size_t cap,const uint8_t *src,size_t n) {
    if (!dst || cap == 0) return;
    dst[0] = 0;
    if (!src) return;
    size_t i=0, out=0;
    while (i<n) {
        unsigned char lead=src[i];
        size_t width= lead<0x80 ? 1 :
            (lead>=0xC2 && lead<0xE0) ? 2 :
            (lead>=0xE0 && lead<0xF0) ? 3 :
            (lead>=0xF0 && lead<0xF5) ? 4 : 0;
        if (!width || i+width>n || out+width>=cap) break;
        bool valid=true;
        for (size_t j=1;j<width;j++) if ((src[i+j]&0xC0)!=0x80) valid=false;
        if (width==3 && ((lead==0xE0 && src[i+1]<0xA0) ||
                         (lead==0xED && src[i+1]>=0xA0))) valid=false;
        if (width==4 && ((lead==0xF0 && src[i+1]<0x90) ||
                         (lead==0xF4 && src[i+1]>=0x90))) valid=false;
        if (!valid) break;
        memcpy(dst+out,src+i,width);
        out+=width;
        i+=width;
    }
    dst[out]=0;
}

bool hub_decoder_feed(hub_decoder_t *d,const uint8_t *p,size_t n,hub_notice_t *out) {
    if (!d || !p || !out || n==0) return false;
    if (d->used>sizeof(d->bytes) || n>sizeof(d->bytes)-d->used) {
        d->used=0;
        return false;
    }
    memcpy(d->bytes+d->used,p,n);
    d->used+=n;
    if(d->used<5) return false;
    const uint8_t *b=d->bytes;
    if (b[0]!=0 || read32(b+1)!=d->expected_uid) {
        d->used=0;
        return false;
    }
    hub_notice_t result={.uid=d->expected_uid};
    size_t off=5;
    const uint8_t ids[]={0,1,3};
    for(size_t i=0;i<3;i++) {
        if(d->used-off<3) return false;
        uint8_t id=b[off];
        uint16_t length=read16(b+off+1);
        if(id!=ids[i] || length>256) {d->used=0;return false;}
        off+=3;
        if(length>d->used-off) return false;
        if(i==0) hub_utf8_copy(result.app,sizeof(result.app),b+off,length);
        if(i==1) hub_utf8_copy(result.title,sizeof(result.title),b+off,length);
        if(i==2) hub_utf8_copy(result.body,sizeof(result.body),b+off,length);
        off+=length;
    }
    if (off!=d->used) {d->used=0;return false;}
    *out=result;
    d->used=0;
    return true;
}
