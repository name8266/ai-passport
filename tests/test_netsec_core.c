#include "netsec/core.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static void frames(void) {
    uint8_t p[256]={0xc0,0};ns_frame_t f;
    assert(!ns_parse_frame(p,1,&f));assert(!ns_parse_frame(p,23,&f));
    p[24]=7;assert(ns_parse_frame(p,26,&f));assert(f.type==0 && f.subtype==12 && f.reason==7 && f.header_len==24);
    p[1]=0x40;assert(ns_parse_frame(p,26,&f));assert(f.reason==0 && f.protected_frame);
    p[0]=0xd4;p[1]=0;assert(ns_parse_frame(p,10,&f));assert(f.type==1 && f.header_len==10);
    memset(p,0,sizeof(p));p[0]=0x88;p[1]=3;assert(!ns_parse_frame(p,31,&f));assert(ns_parse_frame(p,32,&f));assert(f.header_len==32);
    p[1]=0x83;assert(ns_parse_frame(p,36,&f));assert(f.header_len==36);
    p[0]=0xff;assert(!ns_parse_frame(p,sizeof(p),&f));
    memset(p,0,sizeof(p));p[0]=8;p[1]=2;p[10]=42;p[4]=24;
    const uint8_t llc[]={0xaa,0xaa,3,0,0,0,0x88,0x8e};memcpy(p+24,llc,8);
    uint8_t *e=p+32;e[0]=2;e[1]=3;e[3]=95;e[4]=2;e[6]=0x88;e[16]=5;
    assert(ns_parse_frame(p,131,&f));assert(f.eapol_message==1 && f.replay==5 && f.bssid[0]==42 && f.station[0]==24);
    e[5]=1;e[6]=0x08;p[1]=1;assert(ns_parse_frame(p,131,&f));assert(f.eapol_message==2);
    e[5]=3;e[6]=0xc8;p[1]=2;assert(ns_parse_frame(p,131,&f));assert(f.eapol_message==3);
    e[6]=0x08;p[1]=1;assert(ns_parse_frame(p,131,&f));assert(f.eapol_message==4);
    p[1]|=0x40;assert(ns_parse_frame(p,131,&f));assert(!f.eapol_message);
    p[1]=1;e[3]=200;assert(ns_parse_frame(p,131,&f));assert(!f.eapol_message);
    /* Truncation at every boundary and random frame bytes stay in bounds. */
    for(size_t i=0;i<sizeof(p);i++)(void)ns_parse_frame(p,i,&f);
}
static void text(void) {
    char out[32];ns_text(out,sizeof(out),(const uint8_t*)"中文WiFi",10);assert(!strcmp(out,"中文WiFi"));
    ns_text(out,5,(const uint8_t*)"中文",6);assert(!strcmp(out,"中"));
    const uint8_t bad[]={0xc0,0xaf,0xed,0xa0,0x80,0xf4,0x90,0x80,0x80,0x1b};
    ns_text(out,sizeof(out),bad,sizeof(bad));assert(!strchr(out,0x1b));
    ns_text(out,sizeof(out),(const uint8_t*)"A\nB",3);assert(!strcmp(out,"A?B"));
    uint8_t adv[]={2,1,6,4,9,'a','b','c'};ns_ble_name(adv,sizeof(adv),out,sizeof(out));assert(!strcmp(out,"abc"));
    adv[3]=40;ns_ble_name(adv,sizeof(adv),out,sizeof(out));assert(!out[0]);
    uint8_t details[]={2,1,6,3,0xff,0x4c,0,3,3,0x0f,0x18,2,0x0a,0xf8};
    ns_ble_meta_t meta;ns_ble_meta(details,sizeof(details),&meta);
    assert(meta.has_flags && meta.flags==6 && meta.company==0x4c && meta.uuid==0x180f && meta.tx==-8);
    for(size_t i=0;i<sizeof(details);i++)ns_ble_meta(details,i,&meta);
}
static void dns(void) {
    uint8_t p[128];size_t n=ns_dns_query(p,sizeof(p),"_http._tcp.local");assert(n);
    char out[64];size_t off=12;assert(ns_dns_name(p,n,&off,out,sizeof(out)));assert(!strcmp(out,"_http._tcp.local"));
    p[n]=0xc0;p[n+1]=12;off=n;assert(ns_dns_name(p,n+2,&off,out,sizeof(out)));assert(off==n+2);
    p[0]=0xc0;p[1]=0;off=0;assert(!ns_dns_name(p,2,&off,out,sizeof(out)));
    p[0]=63;off=0;assert(!ns_dns_name(p,2,&off,out,sizeof(out)));
    assert(!ns_dns_query(p,15,"_http._tcp.local"));
    for(size_t i=0;i<n;i++) {off=12;(void)ns_dns_name(p,i,&off,out,sizeof(out));}
}
static void metrics(void) {
    uint8_t c[]={1,6,6,14};int8_t r[]={-40,-50,-80,-20};uint16_t count[14];uint32_t score[14];
    ns_channel_scores(c,r,4,count,score);assert(count[1]==1 && count[6]==2);assert(score[6]>score[11]);assert(score[2]>0);
    ns_burst_t b={0};assert(!ns_burst_add(&b,100,2));assert(ns_burst_add(&b,200,2));assert(!ns_burst_add(&b,300,2));
    ns_burst_tick(&b,1000100);assert(!b.alarm && !b.count);
    ns_session_t s[NS_SESSION_MAX]={0};ns_frame_t f={.eapol_message=1,.has_pair=true,.replay=1};f.bssid[0]=1;f.station[0]=2;
    ns_session_observe(s,&f,100);size_t slot=0;for(size_t i=0;i<NS_SESSION_MAX;i++)if(s[i].used)slot=i;
    assert(s[slot].seen==1);ns_session_observe(s,&f,200);assert(s[slot].repeats==1);
    f.eapol_message=2;ns_session_observe(s,&f,300);assert(s[slot].seen==3);
    f.station[0]=3;ns_session_observe(s,&f,400);assert(s[slot].station[0]==2);
    f.station[0]=2;f.eapol_message=1;f.replay=2;ns_session_observe(s,&f,500);assert(s[slot].seen==1);
}
static void pcap(void) {
    uint8_t p[64],h[24],f[36]={8};assert(ns_pcap_header(h)==24);assert(h[0]==0xd4 && h[20]==105);
    assert(ns_pcap_record(p,1234567,f,24,120)==40);assert(p[0]==1 && p[8]==24 && p[12]==120);
    assert(!ns_pcap_record(p,0,f,37,37));assert(!ns_pcap_record(p,0,f,24,10));
}
int main(void) {frames();text();dns();metrics();pcap();puts("NetSec core: PASS");}
