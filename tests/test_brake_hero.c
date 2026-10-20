#include "brake_hero.h"
#include "brake_scores.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static void steps(bh_state_t *s, bh_key_t k, int n) { for (int i=0;i<n;++i) bh_step(s,k); }
static void closef(float a, float b) { assert(fabsf(a-b)<0.002f); }
static bh_state_t drive(void) {
    bh_state_t s; bh_reset(&s,0);
    while (s.speed*3.6f<125) bh_step(&s,BH_THROTTLE);
    bool hold=true;
    while (s.end==BH_RUNNING) {
        if (s.pedal>1460) hold=false;
        if (s.pedal<1280) hold=true;
        bh_step(&s,hold?BH_BRAKE:BH_NONE);
    }
    return s;
}
static bh_state_t timed(unsigned frame_us) {
    bh_state_t s; bh_reset(&s,0); bh_clock_t c; bh_clock_reset(&c,0);
    const int64_t edges[]={0,1000000,3000000,4500000,4800000,6000000,8000000};
    const bh_key_t keys[]={BH_THROTTLE,BH_TENT,BH_THROTTLE,BH_BRAKE,BH_NONE,BH_TENT,BH_NONE};
    unsigned e=0;
    for(int64_t now=0;now<10000000;) {
        int64_t next=now+frame_us;
        if(next>10000000) next=10000000;
        while(e<7 && edges[e]<=next) {
            bh_advance(&s,&c,edges[e]); bh_clock_key(&c,keys[e]); ++e;
        }
        bh_advance(&s,&c,next); now=next;
    }
    return s;
}
int main(int argc,char **argv) {
    if(argc==3) {
        bh_scores_t table;uint8_t bytes[BH_SCORE_BYTES];
        if(!strcmp(argv[1],"--save")) {
            bh_state_t car=drive();bh_scores_clear(&table);assert(bh_scores_add(&table,&car)==0);
            bh_scores_encode(&table,bytes);FILE *f=fopen(argv[2],"wb");assert(f);
            assert(fwrite(bytes,1,sizeof(bytes),f)==sizeof(bytes));assert(!fclose(f));
        } else {
            assert(!strcmp(argv[1],"--restore"));FILE *f=fopen(argv[2],"rb");assert(f);
            assert(fread(bytes,1,sizeof(bytes),f)==sizeof(bytes));assert(!fclose(f));
            assert(bh_scores_decode(&table,bytes,sizeof(bytes)) && table.count==1);
            assert(table.rows[0].peak_mms>27778 && !strcmp(table.rows[0].name,"ANON"));
            puts("Score restoration in a fresh host process: PASS (not a device NVS test)");
        }
        return 0;
    }
    bh_state_t s; bh_reset(&s,0);
    steps(&s,BH_TENT,60); closef(s.tent,40);
    steps(&s,BH_NONE,60); closef(s.tent,20);
    steps(&s,BH_TENT,60); closef(s.tent,60);
    bh_reset(&s,1); steps(&s,BH_THROTTLE,60); closef(s.throttle,65);
    steps(&s,BH_NONE,60); closef(s.throttle,20);
    steps(&s,BH_THROTTLE,120); closef(s.throttle,100);
    bh_reset(&s,0); steps(&s,BH_BRAKE,120);
    closef(s.pedal,1600); assert(!s.brake_failed);
    assert(bh_step(&s,BH_BRAKE)&BH_EV_BREAK); assert(s.brake_failed && s.brake_force==0);
    steps(&s,BH_NONE,60); steps(&s,BH_BRAKE,60);
    assert(s.brake_failed && !s.brake_force);
    bh_reset(&s,0); steps(&s,BH_BRAKE,60); steps(&s,BH_NONE,30); closef(s.pedal,200);
    bh_reset(&s,0); steps(&s,BH_THROTTLE,60); steps(&s,BH_TENT,30);
    closef(s.throttle,42.5f); closef(s.tent,20);
    for(int i=0;i<80;++i) { bh_step(&s,(bh_key_t)(i%3)); assert(s.speed>=0); }
    assert(bh_key_from_mask(7)==BH_TENT && bh_key_from_mask(6)==BH_BRAKE && bh_key_from_mask(0)==BH_NONE);
    assert(bh_air_tent_force(40,100)>bh_air_tent_force(40,50));
    closef(bh_air_tent_force(20,0),0);
    closef(bh_air_tent_force(40,50),bh_air_tent_force(20,50)*4);
    bh_reset(&s,0);
    while(s.zero100_s<0 && s.end==BH_RUNNING) bh_step(&s,BH_THROTTLE);
    assert(s.zero100_s>4 && s.zero100_s<6.5f);
    printf("0-100 with throttle ramp: %.3f s\n",s.zero100_s);
    while(s.end==BH_RUNNING) bh_step(&s,BH_THROTTLE);
    assert(s.end==BH_COLLISION && !bh_rankable(&s));
    bh_reset(&s,0); s.position=499.9f; s.speed=40; s.moved=true;
    bh_step(&s,BH_BRAKE); assert(s.end==BH_COLLISION && s.position==500);
    s=drive(); assert(s.end==BH_STOPPED && bh_rankable(&s) && !s.brake_failed && s.speed==0);
    bh_state_t frozen=s; steps(&s,BH_THROTTLE,1000); assert(!memcmp(&s,&frozen,sizeof(s)));
    bh_reset(&s,0);s.speed=.1f;s.moved=true;s.position=50;
    bh_step(&s,BH_NONE);assert(s.stop_committed);
    steps(&s,BH_THROTTLE,40);assert(s.end==BH_STOPPED && !s.throttle && !s.speed);
    s=drive();
    bh_state_t a=timed(16667), b=timed(33333), c=timed(97000);
    assert(a.ticks==600 && !memcmp(&a,&b,sizeof(a)) && !memcmp(&a,&c,sizeof(a)));
    bh_scores_t t,restore; bh_scores_clear(&t);
    assert(bh_scores_add(&t,&s)==0); s.remaining-=1;
    assert(bh_scores_add(&t,&s)==0); assert(t.rows[0].remaining_mm<t.rows[1].remaining_mm);
    assert(bh_scores_add(&t,&s)==1); // Earlier result wins an exact tie.
    for(int i=0;i<15;++i) { s.peak_speed+=0.1f; bh_scores_add(&t,&s); }
    assert(t.count==10);
    uint8_t data[BH_SCORE_BYTES]; bh_scores_encode(&t,data);
    assert(bh_scores_decode(&restore,data,sizeof(data)) && restore.count==10);
    assert(restore.rows[0].peak_mms==t.rows[0].peak_mms);
    for(unsigned i=0;i<sizeof(data);++i) {
        data[i]^=0x20; assert(!bh_scores_decode(&restore,data,sizeof(data)) && !restore.count); data[i]^=0x20;
    }
    assert(!bh_scores_decode(&restore,data,5));
    s.end=BH_COLLISION; assert(bh_scores_add(&t,&s)==-1);
    s.end=BH_STOPPED; s.peak_speed=20; assert(bh_scores_add(&t,&s)==-1);
    s.peak_speed=NAN; assert(bh_scores_add(&t,&s)==-1);
    bh_reset(&s,3); assert(!s.pedal && !s.throttle && !s.tent && !s.brake_failed && !s.fired);
    steps(&s,BH_NONE,480); assert(s.end==BH_IDLE);
    puts("Brake Hero physics, input timing, replay protection, scores/CRC: PASS");
}
