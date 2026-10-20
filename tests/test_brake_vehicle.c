#include "brake_vehicle.h"
#include "brake_display_math.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint8_t frame[BH_FRAME_BYTES],previous[BH_FRAME_BYTES];
static unsigned count_color(uint8_t color) {
    unsigned count=0;
    for(unsigned i=BH_PALETTE_BYTES;i<BH_FRAME_BYTES;++i)count+=frame[i]==color;
    return count;
}
int main(void) {
    bh_view_t v={.page=BH_DRIVE,.battery=82};bh_reset(&v.car,0);
    float previous_lift=-1;
    for(int i=0;i<=100;i+=25) {
        v.car.tent=(float)i;
        bh_vehicle_pose_t p=bh_vehicle_pose(&v);
        assert(p.tent_opening>=0 && p.tent_opening<=1 && p.roof_lift>previous_lift);
        previous_lift=p.roof_lift;
        bh_render(frame,&v);
        if(i)assert(memcmp(previous,frame,sizeof(frame)));
        memcpy(previous,frame,sizeof(frame));
    }
    v.car.tent=100;float lift=bh_vehicle_pose(&v).roof_lift;
    for(unsigned i=0;i<30;++i)bh_step(&v.car,BH_NONE);
    assert(bh_vehicle_pose(&v).roof_lift<lift);
    v.car.pedal=0;bh_render(frame,&v);assert(!count_color(234));
    /* A straight road needs a centered rear silhouette. Color reflections may
     * differ, but the body must not yaw/roll relative to the road. */
    memset(frame,0,sizeof(frame));v.car.tent=0;
    bh_vehicle_draw(frame+BH_PALETTE_BYTES,&v);
    for(int y=156;y<242;++y) {
        int left=BH_W,right=-1;
        for(int x=0;x<BH_W;++x)if(frame[BH_PALETTE_BYTES+y*BH_W+x]) {
            if(left==BH_W)left=x;
            right=x;
        }
        if(right>=left)assert(abs(left+right-239)<=3);
    }
    v.car.pedal=800;bh_render(frame,&v);assert(count_color(234)>4);
    v.car.brake_failed=true;bh_render(frame,&v);assert(!count_color(234));
    v.page=BH_CRASH;v.car.end=BH_COLLISION;v.animation=15;v.car.speed=30;
    assert(bh_vehicle_pose(&v).crush>.2f);
    bh_render(frame,&v);assert(count_color(238));
    v.car.obstacle=1;v.animation=50;assert(bh_vehicle_pose(&v).fall>.5f);
    uint16_t palette[256],rows[BH_W*2+2];
    uint8_t bgra[1024]={0};bgra[4+2]=255;bgra[8+1]=255;bgra[12]=255;
    bh_rgb565_palette(palette,bgra);
    assert(palette[1]==0x00f8 && palette[2]==0xe007 && palette[3]==0x1f00);
    memset(frame,0,sizeof(frame));memset(frame+BH_PALETTE_BYTES+319*BH_W,1,BH_W);
    for(unsigned i=0;i<BH_W*2+2;++i)rows[i]=0xabba;
    bh_convert_rows(rows+1,frame,319,3,palette);
    assert(rows[0]==0xabba && rows[BH_W+1]==0xabba);
    for(unsigned i=1;i<=BH_W;++i)assert(rows[i]==0x00f8);
    puts("Vehicle opening/retraction, brake lights, collision, cliff and RGB565 DMA bounds: PASS");
}
