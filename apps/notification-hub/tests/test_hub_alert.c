#include "hub_alert.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
int main(void) {
    hub_sound_config_t c=hub_sound_defaults();assert(hub_sound_valid(c));
    assert(!c.enabled);
    c.enabled=true;
    assert(hub_alert_eligible(c,0,0,0,0,false));
    assert(!hub_alert_eligible(c,1,0,10000,0,false));
    assert(!hub_alert_eligible(c,2,0,10000,0,false));
    assert(!hub_alert_eligible(c,0,1,10000,0,false));
    assert(!hub_alert_eligible(c,0,4,10000,0,false));
    assert(hub_alert_eligible(c,0,2,10000,0,false));
    assert(!hub_alert_eligible(c,0,0,1999,0,true));
    assert(hub_alert_eligible(c,0,0,2000,0,true));
    assert(!hub_alert_eligible(c,0,0,1,10,true));
    c.enabled=false;assert(!hub_alert_eligible(c,0,0,10000,0,false));
    c=hub_sound_defaults();c.tone=3;assert(!hub_sound_valid(c));
    c=hub_sound_defaults();c.volume=61;assert(!hub_sound_valid(c));
    for(unsigned tone=0;tone<HUB_TONE_COUNT;tone++) {
        size_t n=hub_tone_samples(tone);assert(n>0 && n<=HUB_SOUND_RATE/3);
        int peak=0;for(size_t i=0;i<n;i++) {int sample=abs(hub_tone_sample(tone,i));assert(sample<=4200);if(sample>peak)peak=sample;}
        assert(peak>3000);assert(hub_tone_sample(tone,0)==0 && hub_tone_sample(tone,n-1)==0 && hub_tone_sample(tone,n)==0);
    }
    assert(hub_tone_samples(3)==0);
    puts("Alert silence, ANCS flags, burst cooldown and bounded PCM chimes: PASS");
}
