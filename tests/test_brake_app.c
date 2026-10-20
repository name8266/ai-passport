#include "brake_app.h"
#include <assert.h>
#include <stdio.h>
int main(void) {
    bh_app_t a;bh_app_init(&a,0);
    bh_app_input(&a,BH_THROTTLE,true,500000);assert(a.view.page==BH_COUNTDOWN);
    bh_app_tick(&a,5000000);assert(a.view.page==BH_COUNTDOWN); // Held menu OK cannot launch throttle.
    bh_app_input(&a,BH_THROTTLE,false,5100000);
    assert(bh_app_tick(&a,5200000)&BH_EV_START);assert(a.view.page==BH_DRIVE);
    bh_app_input(&a,BH_THROTTLE,true,5300000);
    bh_app_tick(&a,10300000);assert(a.view.page==BH_DRIVE && a.view.car.speed>27);
    // A long OK press is exclusively throttle; no return-to-home behavior.
    bh_app_sync(&a,0,10400000);float t=a.view.car.throttle;
    bh_app_tick(&a,11400000);assert(a.view.car.throttle<t);
    // Simulated debounce bounce and rapid transitions never leave two controls rising.
    for(int i=0;i<50;++i) {
        int64_t now=11410000+i*2000;
        bh_app_input(&a,BH_TENT,true,now);bh_app_input(&a,BH_TENT,false,now+500);
        bh_app_input(&a,BH_BRAKE,true,now+1000);bh_app_input(&a,BH_BRAKE,false,now+1500);
    }
    assert(!a.held_mask);
    a.view.page=BH_RESULT;a.input_after=0;a.view.selection=0;
    bh_app_input(&a,BH_THROTTLE,true,12000000);assert(a.view.page==BH_COUNTDOWN);
    assert(!a.view.car.brake_failed && !a.view.car.throttle && !a.view.car.pedal && a.view.car.position==0);
    a.view.page=BH_CRASH;a.page_since=12000000;
    bh_app_tick(&a,13000000);assert(a.view.page==BH_CRASH);
    bh_app_tick(&a,14700000);assert(a.view.page==BH_RESULT);
    puts("Brake Hero navigation, release gate, long OK and retry: PASS");
}
