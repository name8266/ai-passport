#define NETSEC_HOST_TEST 1
#include "netsec/app.h"
#include <assert.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <string.h>
ns_app_t ns;
int64_t esp_timer_get_time(void) {return 1000000;}
esp_err_t esp_netif_get_ip_info(esp_netif_t *p,esp_netif_ip_info_t *out) {
    (void)p;out->ip.addr=inet_addr("192.0.2.10");out->netmask.addr=inet_addr("255.255.255.0");return ESP_OK;
}
#include "../main/netsec/discovery.c"
static size_t name(uint8_t *p,const char *s) {
    uint8_t q[256];size_t n=ns_dns_query(q,sizeof(q),s);assert(n);memcpy(p,q+12,n-16);return n-16;
}
static size_t record(uint8_t *p,const char *owner,uint16_t type,const char *target) {
    memset(p,0,512);p[2]=0x84;p[7]=1;size_t pos=12;pos+=name(p+pos,owner);
    p[pos++]=0;p[pos++]=type;p[pos++]=0;p[pos++]=1;
    p[pos++]=0;p[pos++]=0;p[pos++]=0;p[pos++]=120;
    size_t lenpos=pos;pos+=2;size_t start=pos;
    if(type==33) {memset(p+pos,0,6);p[pos+5]=80;pos+=6;}
    pos+=name(p+pos,target);p[lenpos]=(pos-start)>>8;p[lenpos+1]=pos-start;return pos;
}
int main(void) {
    ns.connected=true;
    assert(in_lan(inet_addr("192.0.2.20")));assert(!in_lan(inet_addr("192.0.3.20")));assert(!in_lan(inet_addr("192.0.2.10")));
    uint8_t p[512];size_t n=record(p,"_http._tcp.local",12,"Lab._http._tcp.local");
    dns_response(p,n,inet_addr("192.0.2.20"));assert(ns.service_count==1);assert(query_count==1 && query_types[0]==33);
    n=record(p,"Lab._http._tcp.local",33,"lab.local");dns_response(p,n,inet_addr("192.0.2.20"));assert(ns.service_count==1 && ns.services[0].port==80);assert(query_count==2);
    for(size_t i=0;i<n;i++)dns_response(p,i,inet_addr("192.0.2.20"));
    query_add("remote.example.com",1);assert(query_count==2);
    char response[]="HTTP/1.1 200 OK\r\nST: upnp:rootdevice\r\nLOCATION: http://192.0.2.30:8000/description.xml\r\n\r\n";
    ssdp_response(response,strlen(response),inet_addr("192.0.2.30"));assert(ns.service_count==2);assert(strstr(ns.services[1].detail,"description.xml"));
    ssdp_response(response,strlen(response),inet_addr("192.0.2.31"));assert(ns.service_count==3);
    char injected[]="HTTP/1.1 200 OK\r\nST: Printer\033[2J\r\n\r\n";
    ssdp_response(injected,strlen(injected),inet_addr("192.0.2.32"));assert(!strchr(ns.services[3].name,27));
    ns_service_t *unicode=service("mDNS","中文实验网络._http._tcp.local");
    assert(unicode==service("mDNS","中文实验网络._http._tcp.local"));
    char long_name[192];memset(long_name,0,sizeof(long_name));
    for(unsigned i=0;i<50;i++)memcpy(long_name+i*3,"中",3);
    detail_text(unicode,"Host: ",long_name);
    size_t rendered=strlen(unicode->detail)-6;assert(rendered%3==0);
    detail_text(unicode,"Host: ","unsafe\033name.local");assert(!strchr(unicode->detail,27));
    /* Compression cycles, large count claims, random datagrams, and every
       truncation are processed without allocating or following remote URLs. */
    uint32_t rnd=1;for(unsigned i=0;i<4000;i++) {
        for(size_t k=0;k<sizeof(p);k++) {rnd=rnd*1664525U+1013904223U;p[k]=rnd>>24;}
        dns_response(p,i%sizeof(p),inet_addr("192.0.2.20"));
    }
    puts("NetSec discovery parsing: PASS");
}
