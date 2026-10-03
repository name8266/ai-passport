#include "worldcam_protocol.h"
#include <assert.h>
#include <stdio.h>
int main(void)
{
    uint8_t h[12] = {'W','C','A','M',192,0,128,0,0x78,0x56,0x34,0x12};
    uint32_t t = 0;
    assert(wc_frame_header(h, sizeof(h), &t) && t == 0x12345678);
    assert(!wc_frame_header(h, 11, &t));
    assert(!wc_frame_header(NULL, 12, &t));
    h[4] = 240; assert(!wc_frame_header(h, 12, &t)); h[4] = 192;
    h[0] = 'X'; assert(!wc_frame_header(h, 12, &t));
    assert(wc_wrap_index(0, -1, 10) == 9);
    assert(wc_wrap_index(9, 1, 10) == 0);
    assert(wc_wrap_index(4, 1, 0) == 0);
    assert(wc_gateway_valid("http://192.168.1.10:8787"));
    assert(wc_gateway_valid("http://worldcam.local:8787"));
    assert(!wc_gateway_valid("https://server"));
    assert(!wc_gateway_valid("http://server/../../"));
    assert(!wc_gateway_valid("http://user:pass@server"));
    assert(!wc_gateway_valid("http://"));
    uint16_t width,height;
    uint8_t full[12]={'W','C','A','M',240,0,160,0,1,0,0,0};
    assert(wc_frame_info(full,12,&width,&height,&t));
    assert(width==240 && height==160 && t==1);
    assert(!wc_frame_header(full,12,&t));
    full[6]=240;assert(!wc_frame_info(full,12,&width,&height,&t));
    uint8_t index[8]={'W','C','I','X',3,0,8,0};size_t count;
    assert(wc_index_header(index,8,&count)&&count==3);
    index[6]=9;assert(!wc_index_header(index,8,&count));index[6]=8;
    index[4]=0;index[5]=9;assert(!wc_index_header(index,8,&count));
    uint8_t record[8]={0,0,0,0,0,0,1,0};wc_point_t point;
    assert(wc_index_point(record,8,&point));
    record[7]=1;assert(!wc_index_point(record,8,&point));record[7]=0;
    record[6]=128;assert(!wc_index_point(record,8,&point));
    int x,y;wc_map_project(9000,-18000,&x,&y);assert(x==0&&y==0);
    wc_map_project(-9000,18000,&x,&y);assert(x==215&&y==127);
    wc_map_project(0,0,&x,&y);assert(x==107&&y==63);
    wc_point_t points[3]={{.flags=0},{.flags=1},{.flags=1}};size_t chosen;
    for(unsigned r=0;r<20;++r){assert(wc_random_index(points,3,r,1,&chosen));assert(chosen==2);}
    points[2].flags=0;assert(wc_random_index(points,3,2,1,&chosen)&&chosen==1);
    points[1].flags=0;assert(!wc_random_index(points,3,0,0,&chosen));
    assert(wc_utf8_valid("中文地名 ABC"));
    assert(!wc_utf8_valid("\xe4\xb8"));
    assert(!wc_utf8_valid("\xc0\xaf"));
    assert(!wc_utf8_valid("\xed\xa0\x80"));
    assert(!wc_utf8_valid("line\nfeed"));
    puts("WorldCam protocol: PASS");
}
