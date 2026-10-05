#include "app.h"
#include "esp_timer.h"
#include "lwip/sockets.h"
#include "lwip/inet.h"
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <fcntl.h>

static int mdns=-1,ssdp=-1;
static uint64_t end_time,send_time;
static char queries[20][97];
static uint16_t query_types[20];
static unsigned query_count,query_next;
static uint16_t be16(const uint8_t *p) {return (uint16_t)p[0]<<8|p[1];}
static uint32_t be32(const uint8_t *p) {return (uint32_t)p[0]<<24|(uint32_t)p[1]<<16|(uint32_t)p[2]<<8|p[3];}
static bool local_name(const char *s) {size_t n=strlen(s);return n>=6 && !strcasecmp(s+n-6,".local");}
static void query_add(const char *name,uint16_t type) {
    if(!local_name(name) || query_count>=20 || strlen(name)>=sizeof(queries[0]))return;
    for(unsigned i=0;i<query_count;i++)if(query_types[i]==type && !strcmp(queries[i],name))return;
    snprintf(queries[query_count],sizeof(queries[0]),"%s",name);query_types[query_count++]=type;
}
static ns_service_t *service(const char *kind,const char *name) {
    char display[97];ns_text(display,sizeof(display),(const uint8_t*)name,strlen(name));
    for(size_t i=0;i<ns.service_count;i++)
        if(!strcmp(ns.services[i].kind,kind) && !strcmp(ns.services[i].name,display))return &ns.services[i];
    if(ns.service_count==NS_SERVICE_MAX)return NULL;
    ns_service_t *s=&ns.services[ns.service_count++];memset(s,0,sizeof(*s));
    snprintf(s->kind,sizeof(s->kind),"%s",kind);
    snprintf(s->name,sizeof(s->name),"%s",display);return s;
}
static void detail_text(ns_service_t *s,const char *prefix,const char *wire_name) {
    char safe[112];ns_text(safe,sizeof(safe),(const uint8_t*)wire_name,strlen(wire_name));
    snprintf(s->detail,sizeof(s->detail),"%s%s",prefix,safe);
}
static bool in_lan(uint32_t ip) {
    esp_netif_ip_info_t info;
    if(!ns.connected || esp_netif_get_ip_info(ns.netif,&info)!=ESP_OK)return false;
    return (ip&info.netmask.addr)==(info.ip.addr&info.netmask.addr) && ip!=info.ip.addr;
}
static void dns_response(const uint8_t *p,size_t n,uint32_t ip) {
    if(n<12 || !(p[2]&0x80) || (p[3]&15))return;
    unsigned q=be16(p+4),r=(unsigned)be16(p+6)+be16(p+8)+be16(p+10);
    if(q>16 || r>64)return;
    size_t off=12;char owner[192],target[192];
    for(unsigned i=0;i<q;i++) {
        if(!ns_dns_name(p,n,&off,owner,sizeof(owner)) || n-off<4)return;
        off+=4;
    }
    for(unsigned i=0;i<r;i++) {
        if(!ns_dns_name(p,n,&off,owner,sizeof(owner)) || n-off<10)return;
        uint16_t type=be16(p+off),cls=be16(p+off+2)&0x7fff;
        uint32_t ttl=be32(p+off+4);size_t len=be16(p+off+8);off+=10;
        if(len>n-off)return;
        if(cls!=1 || !local_name(owner)) {off+=len;continue;}
        ns_service_t *s=NULL;
        if(type==12) {
            size_t ptr=off;
            if(ns_dns_name(p,n,&ptr,target,sizeof(target)) && ptr<=off+len && local_name(target)) {
                if(!strcmp(owner,"_services._dns-sd._udp.local"))query_add(target,12);
                else query_add(target,33);
                s=service("mDNS",target);
                if(s)detail_text(s,"PTR: ",owner);
            }
        } else if(type==33 && len>=7) {
            size_t ptr=off+6;
            if(ns_dns_name(p,n,&ptr,target,sizeof(target)) && ptr<=off+len && local_name(target)) {
                query_add(target,1);s=service("mDNS",owner);
                if(s) {s->port=be16(p+off+4);detail_text(s,"Host: ",target);}
            }
        } else if(type==1 && len==4) {
            uint32_t address;memcpy(&address,p+off,4);
            if(in_lan(address)) {s=service("mDNS",owner);if(s) {s->ip=address;snprintf(s->detail,sizeof(s->detail),"IPv4 host");}}
        }
        if(s) {s->ttl=(uint32_t)(esp_timer_get_time()/1000000)+(ttl>3600?3600:ttl);if(!s->ip)s->ip=ip;}
        off+=len;
    }
}
static bool field(const char *p,const char *key,char *out,size_t cap) {
    const char *line=strstr(p,"\r\n");if(!line)return false;
    while((line+=2) && *line) {
        const char *end=strstr(line,"\r\n");if(!end)return false;
        size_t k=strlen(key);
        if((size_t)(end-line)>k && !strncasecmp(line,key,k) && line[k]==':') {
            const char *v=line+k+1;while(v<end && *v==' ')v++;
            ns_text(out,cap,(const uint8_t*)v,end-v);return true;
        }
        line=end;
    }
    return false;
}
static void ssdp_response(char *p,size_t n,uint32_t ip) {
    if(n<12 || strncmp(p,"HTTP/1.1 200",12))return;
    char name[97],detail[129];
    if(!field(p,"ST",name,sizeof(name)))return;
    if(!field(p,"LOCATION",detail,sizeof(detail)))snprintf(detail,sizeof(detail),"No LOCATION");
    /* Key by service + source: different devices advertising the same ST survive. */
    char key[128];struct in_addr addr={.s_addr=ip};
    snprintf(key,sizeof(key),"%.65s @ %s",name,inet_ntoa(addr));
    ns_service_t *s=service("SSDP",key);
    if(s) {snprintf(s->detail,sizeof(s->detail),"%s",detail);s->ip=ip;s->ttl=esp_timer_get_time()/1000000+120;}
}
void ns_discovery_stop(void) {
    if(mdns>=0)close(mdns);
    if(ssdp>=0)close(ssdp);
    mdns=ssdp=-1;end_time=0;
}
void ns_discovery_start(void) {
    ns_discovery_stop();
    if(!ns.connected) {snprintf(ns.status,sizeof(ns.status),"Open LAN and connect first");return;}
    ns.service_count=0;query_count=query_next=0;
    esp_netif_ip_info_t info;if(esp_netif_get_ip_info(ns.netif,&info)!=ESP_OK)return;
    mdns=socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP);ssdp=socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP);
    if(mdns<0 || ssdp<0)goto fail;
    int yes=1;setsockopt(mdns,SOL_SOCKET,SO_REUSEADDR,&yes,sizeof(yes));
    struct sockaddr_in bind_addr={.sin_family=AF_INET,.sin_port=htons(5353),.sin_addr.s_addr=INADDR_ANY};
    if(bind(mdns,(struct sockaddr*)&bind_addr,sizeof(bind_addr))<0)goto fail;
    struct ip_mreq join={.imr_multiaddr.s_addr=inet_addr("224.0.0.251"),.imr_interface.s_addr=info.ip.addr};
    if(setsockopt(mdns,IPPROTO_IP,IP_ADD_MEMBERSHIP,&join,sizeof(join))<0)goto fail;
    unsigned char ttl=1;setsockopt(mdns,IPPROTO_IP,IP_MULTICAST_TTL,&ttl,sizeof(ttl));
    setsockopt(ssdp,IPPROTO_IP,IP_MULTICAST_TTL,&ttl,sizeof(ttl));
    fcntl(mdns,F_SETFL,O_NONBLOCK);fcntl(ssdp,F_SETFL,O_NONBLOCK);
    static const char request[]="M-SEARCH * HTTP/1.1\r\nHOST: 239.255.255.250:1900\r\nMAN: \"ssdp:discover\"\r\nMX: 1\r\nST: ssdp:all\r\n\r\n";
    struct sockaddr_in dst={.sin_family=AF_INET,.sin_port=htons(1900),.sin_addr.s_addr=inet_addr("239.255.255.250")};
    if(sendto(ssdp,request,sizeof(request)-1,0,(struct sockaddr*)&dst,sizeof(dst))<0)goto fail;
    query_add("_services._dns-sd._udp.local",12);
    query_add("_http._tcp.local",12);query_add("_ipp._tcp.local",12);
    query_add("_ssh._tcp.local",12);query_add("_googlecast._tcp.local",12);
    end_time=esp_timer_get_time()+8000000;send_time=0;
    snprintf(ns.status,sizeof(ns.status),"Local discovery: 8 seconds");return;
fail:
    ns_discovery_stop();snprintf(ns.status,sizeof(ns.status),"Discovery socket setup failed");
}
void ns_discovery_tick(uint64_t now) {
    if(!end_time)return;
    if(now>end_time) {
        ns_discovery_stop();snprintf(ns.status,sizeof(ns.status),"%u service records",(unsigned)ns.service_count);return;
    }
    if(query_next<query_count && now-send_time>=250000) {
        uint8_t q[160];size_t n=ns_dns_query(q,sizeof(q),queries[query_next]);
        if(n) {
            q[n-4]=query_types[query_next]>>8;q[n-3]=query_types[query_next];
            struct sockaddr_in dst={.sin_family=AF_INET,.sin_port=htons(5353),.sin_addr.s_addr=inet_addr("224.0.0.251")};
            (void)sendto(mdns,q,n,0,(struct sockaddr*)&dst,sizeof(dst));
        }
        query_next++;send_time=now;
    }
    /* At most four datagrams per tick; discovery traffic cannot starve buttons. */
    static uint8_t buffer[768];
    for(int i=0;i<4;i++) {
        struct sockaddr_in from;socklen_t flen=sizeof(from);
        int fd=(i&1)?ssdp:mdns;
        ssize_t n=recvfrom(fd,buffer,sizeof(buffer)-1,0,(struct sockaddr*)&from,&flen);
        if(n<=0 || !in_lan(from.sin_addr.s_addr))continue;
        buffer[n]=0;
        if(fd==mdns && ntohs(from.sin_port)==5353)dns_response(buffer,n,from.sin_addr.s_addr);
        else if(fd==ssdp)ssdp_response((char*)buffer,n,from.sin_addr.s_addr);
    }
}
