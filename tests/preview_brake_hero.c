#define _POSIX_C_SOURCE 200809L
#include "brake_render.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
static uint8_t frame[BH_FRAME_BYTES];
static void capture(const char *name,bh_view_t *v) {
    bh_render(frame,v);
    char path[160];snprintf(path,sizeof(path),"build/brake-hero/%s.ppm",name);
    FILE *f=fopen(path,"wb");assert(f);fprintf(f,"P6\n240 320\n255\n");
    for(int i=0;i<BH_W*BH_H;++i) {
        const uint8_t *c=frame+frame[BH_PALETTE_BYTES+i]*4;
        const uint8_t rgb[]={c[2],c[1],c[0]};fwrite(rgb,1,3,f);
    }
    fclose(f);
}
static double now(void) {struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec+t.tv_nsec*1e-9;}
int main(int argc,char **argv) {
    bh_view_t v={.page=BH_HOME,.battery=86,.buttons_ok=true,.storage_ok=true,.audio_ok=true,.rank=-1};
    if(argc>1 && !strcmp(argv[1],"--animate")) {
        bh_reset(&v.car,2);v.page=BH_DRIVE;
        bool braking=false,hold=true;
        for(int f=0;f<210;++f) {
            for(int tick=0;tick<4;++tick) {
                if(v.car.remaining<v.car.speed*v.car.speed/14+v.car.speed*1.15f)braking=true;
                if(v.car.pedal>1460)hold=false;
                if(v.car.pedal<1280)hold=true;
                bh_step(&v.car,!braking?BH_THROTTLE:hold?BH_BRAKE:BH_NONE);
            }
            if(v.car.end!=BH_RUNNING)v.page=v.car.end==BH_STOPPED?BH_RESULT:BH_CRASH;
            v.animation=(unsigned)f;
            char name[48];snprintf(name,sizeof(name),"motion-%03d",f);capture(name,&v);
        }
        return 0;
    }
    bh_reset(&v.car,2);bh_scores_clear(&v.scores);capture("home",&v);
    v.page=BH_COUNTDOWN;v.countdown=3;capture("countdown",&v);
    v.page=BH_DRIVE;
    while(v.car.speed*3.6f<130)bh_step(&v.car,BH_THROTTLE);
    capture("driving",&v);
    for(unsigned opening=0;opening<=100;opening+=25) {
        char name[40];v.car.tent=(float)opening;
        snprintf(name,sizeof(name),"tent-%03u",opening);capture(name,&v);
    }
    v.car.tent=0;
    while(v.car.pedal<1400 && v.car.end==BH_RUNNING)bh_step(&v.car,BH_BRAKE);
    capture("warning",&v);
    while(!v.car.brake_failed && v.car.end==BH_RUNNING)bh_step(&v.car,BH_BRAKE);
    assert(v.car.brake_failed);capture("brake-fail",&v);
    while(v.car.end==BH_RUNNING)bh_step(&v.car,BH_THROTTLE);
    assert(v.car.end==BH_COLLISION);
    v.page=BH_CRASH;v.animation=8;capture("collision",&v);
    bh_reset(&v.car,0);
    while(v.car.speed*3.6f<135)bh_step(&v.car,BH_THROTTLE);
    bool hold=true;
    while(v.car.end==BH_RUNNING) {
        if(v.car.pedal>1460)hold=false;
        if(v.car.pedal<1280)hold=true;
        bh_step(&v.car,hold?BH_BRAKE:BH_NONE);
    }
    v.page=BH_RESULT;v.rank=bh_scores_add(&v.scores,&v.car);v.saved=true;capture("result",&v);
    v.page=BH_BOARD;capture("leaderboard",&v);v.page=BH_HELP;capture("help",&v);
    assert(bh_font_has(0x5239) && bh_font_has('?') && !bh_font_has(0x9f98));
    uint8_t *guard=malloc(BH_FRAME_BYTES+32);assert(guard);memset(guard,0xab,BH_FRAME_BYTES+32);
    for(int page=BH_HOME;page<=BH_HELP;++page) {
        v.page=(bh_page_t)page;bh_render(guard+16,&v);
        for(int i=0;i<16;++i)assert(guard[i]==0xab && guard[BH_FRAME_BYTES+16+i]==0xab);
    }
    free(guard);
    v.page=BH_DRIVE;
    // Close obstacles and tall roadside sprites cannot overwrite the top HUD.
    uint8_t header_copy[BH_W*36];
    v.car.remaining=500;bh_render(frame,&v);memcpy(header_copy,frame+BH_PALETTE_BYTES,sizeof(header_copy));
    for(unsigned obstacle=0;obstacle<4;++obstacle) {
        v.car.obstacle=obstacle;v.car.remaining=.01f;bh_render(frame,&v);
        assert(!memcmp(header_copy,frame+BH_PALETTE_BYTES,sizeof(header_copy)));
    }
    double start=now(),maximum=0;
    double samples[300];
    for(int i=0;i<300;++i) {
        v.car.position=i*1.631f;v.car.remaining=500-v.car.position;v.animation=i;
        double t=now();bh_render(frame,&v);samples[i]=(now()-t)*1000;
        if(samples[i]>maximum)maximum=samples[i];
    }
    double total=(now()-start)*1000;
    for(int i=0;i<300;++i)for(int j=i+1;j<300;++j)if(samples[j]<samples[i]){double t=samples[j];samples[j]=samples[i];samples[i]=t;}
    printf("Desktop shared-renderer CPU timing (NOT ESP32): mean %.3f ms, P95 %.3f ms, max %.3f ms\n",total/300,samples[284],maximum);
    puts("Renderer bounds and glyph positive/negative probes: PASS");
}
