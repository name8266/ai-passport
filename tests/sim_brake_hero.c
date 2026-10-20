#include "brake_hero.h"
#include <stdio.h>
#include <stdint.h>
static uint32_t rng=0x42524831;
static unsigned random_u(void) { rng^=rng<<13; rng^=rng>>17; rng^=rng<<5; return rng; }
int main(void) {
    puts("id,strategy,peak_kmh,success,qualified,brake_failed,remaining_m,position_m,zero100_s,max_pedal_n,max_tent");
    for(unsigned i=0;i<10000;++i) {
        unsigned kind=i%4; bh_state_t s; bh_reset(&s,i%4);
        float target=105+(random_u()%3500)/100.0f;
        float margin=0.05f+(random_u()%270)/100.0f;
        bool braking=false,hold=true;
        bh_key_t key=BH_THROTTLE;
        while(s.end==BH_RUNNING) {
            if(kind==0 && s.peak_speed*3.6f>=target) braking=true;
            if(kind==1 && s.remaining < s.speed*s.speed/(2*7.0f)+s.speed*margin) braking=true;
            if(kind==2 && s.peak_speed*3.6f>=target+10) braking=true;
            if(kind==3) {
                if(s.ticks%30==0) key=(bh_key_t)((int)(random_u()%4)-1);
            } else if(!braking) key=BH_THROTTLE;
            else if(kind==2) key=BH_BRAKE; // Reckless full brake, then tent rescue.
            else {
                if(s.pedal>1450+(i%5)*10) hold=false;
                if(s.pedal<1260) hold=true;
                key=hold?BH_BRAKE:BH_NONE;
            }
            if(kind==2 && s.brake_failed) key=BH_TENT;
            bh_step(&s,key);
        }
        printf("%u,%s,%.4f,%u,%u,%u,%.4f,%.4f,%.4f,%.2f,%.2f\n",i,
            (const char*[]){"conservative","late_brake","break_tent_rescue","random"}[kind],
            s.peak_speed*3.6f,s.end==BH_STOPPED,bh_rankable(&s),s.brake_failed,s.remaining,s.position,
            s.zero100_s,s.max_pedal,s.max_tent);
    }
}
