#include "worldcam_protocol.h"
#include <ctype.h>
#include <string.h>
static uint16_t le16(const uint8_t *p) {return p[0]|((uint16_t)p[1]<<8);}
bool wc_frame_info(const uint8_t *h,size_t size,uint16_t *width,uint16_t *height,uint32_t *acquired)
{
    if (!h||!width||!height||!acquired||size!=WC_HEADER_BYTES||memcmp(h,"WCAM",4)) return false;
    unsigned w=le16(h+4),v=le16(h+6);
    if (!((w==WC_WIDTH&&v==WC_HEIGHT)||(w==WC_FULL_WIDTH&&v==WC_FULL_HEIGHT))) return false;
    *width=w;*height=v;
    *acquired=(uint32_t)h[8]|((uint32_t)h[9]<<8)|((uint32_t)h[10]<<16)|((uint32_t)h[11]<<24);
    return true;
}
bool wc_frame_header(const uint8_t *h,size_t size,uint32_t *acquired)
{
    uint16_t w,v;return wc_frame_info(h,size,&w,&v,acquired)&&w==WC_WIDTH&&v==WC_HEIGHT;
}
bool wc_index_header(const uint8_t *h,size_t size,size_t *count)
{
    if (!h||!count||size!=WC_INDEX_HEADER_BYTES||memcmp(h,"WCIX",4)||le16(h+6)!=WC_INDEX_RECORD_BYTES) return false;
    *count=le16(h+4);return *count>0&&*count<=WC_LOCATION_LIMIT;
}
bool wc_index_point(const uint8_t *r,size_t size,wc_point_t *p)
{
    if (!r||!p||size!=WC_INDEX_RECORD_BYTES) return false;
    p->index=le16(r);p->lat=(int16_t)le16(r+2);p->lon=(int16_t)le16(r+4);p->flags=r[6];
    return p->index<WC_LOCATION_LIMIT&&p->lat>=-9000&&p->lat<=9000&&p->lon>=-18000&&p->lon<=18000&&!(p->flags&~7)&&r[7]==0;
}
void wc_map_project(int16_t lat,int16_t lon,int *x,int *y)
{
    if (lat>9000)lat=9000;
    if (lat< -9000)lat= -9000;
    if (lon>18000)lon=18000;
    if (lon< -18000)lon= -18000;
    *x=((int32_t)lon+18000)*(WC_MAP_WIDTH-1)/36000;
    *y=(9000-(int32_t)lat)*(WC_MAP_HEIGHT-1)/18000;
}
size_t wc_wrap_index(size_t index,int direction,size_t count)
{
    if (!count)return 0;
    index%=count;return direction<0?(index?index-1:count-1):(index+1)%count;
}
bool wc_random_index(const wc_point_t *points,size_t count,uint32_t random,size_t current,size_t *chosen)
{
    if (!points||!chosen)return false;
    size_t eligible=0;
    for(size_t i=0;i<count;++i)if((points[i].flags&WC_FLAG_AVAILABLE)&&i!=current)++eligible;
    if(!eligible){if(current<count&&(points[current].flags&WC_FLAG_AVAILABLE)){*chosen=current;return true;}return false;}
    size_t pick=random%eligible;
    for(size_t i=0;i<count;++i)if((points[i].flags&WC_FLAG_AVAILABLE)&&i!=current){if(!pick){*chosen=i;return true;}--pick;}
    return false;
}
bool wc_gateway_valid(const char *url)
{
    if(!url||strncmp(url,"http://",7)||strlen(url)>=160)return false;
    const char *host=url+7;if(!*host||*host==':'||*host=='.')return false;
    for(const char *p=host;*p;++p)if(!(isalnum((unsigned char)*p)||*p=='.'||*p=='-'||*p==':'))return false;
    return true;
}
bool wc_utf8_valid(const char *s)
{
    if(!s)return false;
    const unsigned char *p=(const unsigned char *)s;
    while(*p){
        if(*p<0x80){if(*p<32)return false;++p;continue;}
        uint32_t code;unsigned n;
        if(*p>=0xc2&&*p<=0xdf){code=*p&31;n=1;}
        else if(*p>=0xe0&&*p<=0xef){code=*p&15;n=2;}
        else if(*p>=0xf0&&*p<=0xf4){code=*p&7;n=3;}
        else return false;
        ++p;
        for(unsigned i=0;i<n;++i){if((*p&0xc0)!=0x80)return false;code=(code<<6)|(*p++&63);}
        if((n==2&&code<0x800)||(n==3&&code<0x10000)||code>0x10ffff||(code>=0xd800&&code<=0xdfff))return false;
    }
    return true;
}
