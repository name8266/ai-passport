#include "core.h"
#include <string.h>

static uint16_t le16(const uint8_t *p) { return p[0] | (uint16_t)p[1] << 8; }
static uint16_t be16(const uint8_t *p) { return (uint16_t)p[0] << 8 | p[1]; }
static void put32(uint8_t *p, uint32_t v) {
    for (unsigned i = 0; i < 4; i++) p[i] = (uint8_t)(v >> (i * 8));
}

bool ns_parse_frame(const uint8_t *p, size_t n, ns_frame_t *o) {
    memset(o, 0, sizeof(*o));
    if (n < 2 || (p[0] & 3)) return false;
    uint16_t fc = le16(p);
    o->type = (fc >> 2) & 3; o->subtype = (fc >> 4) & 15;
    o->retry = (fc & 0x0800) != 0;
    o->protected_frame = (fc & 0x4000) != 0;
    size_t h = o->type == 1 ? ((o->subtype == 12 || o->subtype == 13) ? 10 : 16) : 24;
    if (o->type > 2 || n < h) return false;
    unsigned ds = (fc >> 8) & 3;
    if (o->type == 2) {
        if (ds == 3) h += 6;
        if (o->subtype & 8) { h += 2; if (fc & 0x8000) h += 4; }
    }
    if (n < h || h > NS_SNAPLEN) return false;
    o->header_len = h;
    if (o->type != 1) {
        memcpy(o->bssid, p + (ds == 1 ? 4 : ds == 2 ? 10 : 16), 6);
        if (o->type == 2 && (ds == 1 || ds == 2)) {
            memcpy(o->station, p + (ds == 1 ? 10 : 4), 6); o->has_pair = true;
        }
    }
    if (o->type == 0 && (o->subtype == 10 || o->subtype == 12) &&
        !o->protected_frame && n >= 26) o->reason = le16(p + 24);
    static const uint8_t llc[] = {0xaa,0xaa,0x03,0x00,0x00,0x00,0x88,0x8e};
    /* Protected/A-MSDU/WDS payloads are not interpreted as plaintext EAPOL. */
    if (o->type != 2 || !o->has_pair || o->protected_frame ||
        ((o->subtype & 8) && (p[ds == 3 ? 30 : 24] & 0x80)) ||
        n < h + 8 + 99 || memcmp(p + h, llc, 8)) return true;
    const uint8_t *e = p + h + 8;
    if (e[1] != 3 || (e[4] != 2 && e[4] != 254) ||
        be16(e + 2) < 95 || be16(e + 2) > n - h - 12) return true;
    uint16_t ki = be16(e + 5);
    if (!(ki & 8) || (ki & (0x0400 | 0x0800 | 0x2000))) return true;
    bool ack = ki & 0x80, mic = ki & 0x100, install = ki & 0x40, secure = ki & 0x200;
    bool from_ap = ds == 2;
    if (ack && !mic && !install && from_ap) o->eapol_message = 1;
    else if (!ack && mic && !secure && !install && !from_ap) o->eapol_message = 2;
    else if (ack && mic && install && from_ap) o->eapol_message = 3;
    else if (!ack && mic && secure && !install && !from_ap) o->eapol_message = 4;
    for (unsigned i = 0; i < 8; i++) o->replay = o->replay << 8 | e[9+i];
    return true;
}

void ns_text(char *out, size_t cap, const uint8_t *p, size_t n) {
    size_t j = 0;
    if (!cap) return;
    for (size_t i = 0; i < n && p[i];) {
        uint32_t cp = p[i]; size_t w = 1;
        bool valid = true;
        if (cp >= 0xc2 && cp <= 0xdf) { cp &= 31; w = 2; }
        else if (cp >= 0xe0 && cp <= 0xef) { cp &= 15; w = 3; }
        else if (cp >= 0xf0 && cp <= 0xf4) { cp &= 7; w = 4; }
        else if (cp >= 0x80) valid = false;
        if (i+w > n) valid = false;
        if (valid) for (size_t k = 1; k < w; k++) {
            if ((p[i+k]&0xc0) != 0x80) { valid = false; break; }
            cp = cp << 6 | (p[i+k]&63);
        }
        if ((w == 2 && cp < 0x80) || (w == 3 && cp < 0x800) ||
            (w == 4 && (cp < 0x10000 || cp > 0x10ffff)) ||
            (cp >= 0xd800 && cp <= 0xdfff)) valid = false;
        bool supported = (cp >= 32 && cp <= 126) || (cp >= 0x3000 && cp <= 0x303f) ||
                         (cp >= 0x4e00 && cp <= 0x9fef) || (cp >= 0xff01 && cp <= 0xff60);
        if (!valid || !supported) {
            if (j+1 >= cap) break;
            out[j++] = '?'; i += valid ? w : 1;
        } else {
            if (j+w >= cap) break;
            memcpy(out+j, p+i, w); j += w; i += w;
        }
    }
    out[j] = 0;
}
void ns_ble_name(const uint8_t *p, size_t n, char *out, size_t cap) {
    if (cap) out[0] = 0;
    for (size_t i = 0; i < n;) {
        size_t len = p[i]; if (!len || len > n-i-1) break;
        if (p[i+1] == 8 || p[i+1] == 9) {
            ns_text(out, cap, p+i+2, len-1); if (p[i+1] == 9) return;
        }
        i += len+1;
    }
}
void ns_ble_meta(const uint8_t *p, size_t n, ns_ble_meta_t *out) {
    memset(out,0,sizeof(*out));
    for(size_t i=0;i<n;) {
        size_t len=p[i];if(!len || len>n-i-1)break;
        uint8_t type=p[i+1];
        if(type==1 && len>=2) {out->has_flags=true;out->flags=p[i+2];}
        if(type==0xff && len>=3) {out->has_company=true;out->company=le16(p+i+2);}
        if((type==2 || type==3) && len>=3) {out->has_uuid=true;out->uuid=le16(p+i+2);}
        if(type==0x0a && len>=2) {out->has_tx=true;out->tx=(int8_t)p[i+2];}
        i+=len+1;
    }
}
void ns_channel_scores(const uint8_t *c, const int8_t *r, size_t n,
                       uint16_t count[14], uint32_t score[14]) {
    memset(count,0,14*sizeof(*count)); memset(score,0,14*sizeof(*score));
    for (size_t i=0;i<n;i++) {
        if (c[i]<1 || c[i]>13) continue;
        count[c[i]]++;
        int strength = r[i]+100; if (strength<1) strength=1; if(strength>80) strength=80;
        for (int ch=1;ch<=13;ch++) {
            int d=ch-c[i]; if(d<0)d=-d;
            if(d<5) score[ch] += (5-d)*strength;
        }
    }
}
bool ns_dns_name(const uint8_t *p, size_t n, size_t *off, char *out, size_t cap) {
    size_t pos=*off, end=0,j=0; unsigned steps=0;
    if (!cap) return false;
    while(pos<n && ++steps<=64) {
        uint8_t l=p[pos++];
        if(!l) { *off=end?end:pos; out[j]=0; return true; }
        if((l&0xc0)==0xc0) {
            if(pos>=n) return false;
            size_t target=(size_t)(l&63)<<8 | p[pos++];
            if(!end)end=pos;
            if(target>=n) return false;
            pos=target; continue;
        }
        if(l&0xc0 || l>63 || l>n-pos || j+l+1>=cap) return false;
        if(j)out[j++]='.';
        for(unsigned k=0;k<l;k++) out[j++]=(p[pos+k]>=32 && p[pos+k]!=127)?p[pos+k]:'?';
        pos+=l;
    }
    return false;
}
size_t ns_dns_query(uint8_t *p,size_t cap,const char *name) {
    if(cap<18)return 0;
    memset(p,0,12); p[5]=1; size_t j=12;
    while(*name) {
        const char *dot=strchr(name,'.'); size_t l=dot?(size_t)(dot-name):strlen(name);
        if(!l || l>63 || j+l+6>cap)return 0;
        p[j++]=l; memcpy(p+j,name,l);j+=l;
        if(!dot)break;
        name=dot+1;
    }
    p[j++]=0;p[j++]=0;p[j++]=12;p[j++]=0;p[j++]=1;
    return j;
}
size_t ns_pcap_header(uint8_t out[24]) {
    memset(out,0,24); put32(out,0xa1b2c3d4);out[4]=2;out[6]=4;
    put32(out+16,NS_SNAPLEN);put32(out+20,105);return 24;
}
size_t ns_pcap_record(uint8_t *out,uint64_t us,const uint8_t *p,uint32_t saved,uint32_t orig) {
    if(saved>NS_SNAPLEN || saved>orig)return 0;
    put32(out,us/1000000);put32(out+4,us%1000000);put32(out+8,saved);put32(out+12,orig);
    memcpy(out+16,p,saved);return saved+16;
}
void ns_burst_tick(ns_burst_t *b,uint64_t now) {
    if(now-b->start>=1000000) { b->start=now;b->count=0;b->alarm=false; }
}
bool ns_burst_add(ns_burst_t *b,uint64_t now,uint16_t threshold) {
    ns_burst_tick(b,now); b->count++;
    if(!b->alarm && b->count>=threshold) { b->alarm=true;return true; }return false;
}
void ns_session_observe(ns_session_t s[NS_SESSION_MAX],const ns_frame_t *f,uint64_t now) {
    if(!f->eapol_message || !f->has_pair)return;
    size_t slot=0;uint64_t oldest=UINT64_MAX;
    for(size_t i=0;i<NS_SESSION_MAX;i++) {
        if(s[i].used && !memcmp(s[i].bssid,f->bssid,6) && !memcmp(s[i].station,f->station,6)) {
            slot=i;break;
        }
        if(!s[i].used) { oldest=0;slot=i; }
        else if(s[i].last_us<oldest) { oldest=s[i].last_us;slot=i; }
    }
    ns_session_t *x=&s[slot];
    bool same=x->used && !memcmp(x->bssid,f->bssid,6) && !memcmp(x->station,f->station,6);
    if(!same || now-x->last_us>30000000 ||
       (f->eapol_message==1 && (!same || f->replay!=x->replay))) {
        memset(x,0,sizeof(*x));x->used=true;
        memcpy(x->bssid,f->bssid,6);memcpy(x->station,f->station,6);
    }
    uint8_t mask=1U<<(f->eapol_message-1);
    if(x->seen&mask)x->repeats++;
    x->seen|=mask;x->last_us=now;x->replay=f->replay;
}
