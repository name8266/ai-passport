#include "hub_alert.h"
#include <math.h>
hub_sound_config_t hub_sound_defaults(void) {return (hub_sound_config_t){false,0,30};}
bool hub_sound_valid(hub_sound_config_t c) {return c.tone<HUB_TONE_COUNT && c.volume>=10 && c.volume<=60;}
bool hub_alert_eligible(hub_sound_config_t c,uint8_t event,uint8_t flags,uint64_t now,uint64_t last,bool has_last) {
    /* ANCS silent/pre-existing flags: Apple's official ANCS Appendix. */
    return c.enabled && hub_sound_valid(c) && event==0 && !(flags&5) &&
        (!has_last || (now>=last && now-last>=2000));
}
size_t hub_tone_samples(uint8_t tone) {return tone<HUB_TONE_COUNT?(tone==2?4160:3200):0;}
int16_t hub_tone_sample(uint8_t tone,size_t index) {
    size_t n=hub_tone_samples(tone);if(index>=n) return 0;
    bool second=tone==2 && index>=2080;
    if(tone==2) {index%=2080;n=2080;}
    float envelope=index<80?(float)index/80.0f:(float)(n-1-index)/1200.0f;
    if(envelope>1) envelope=1;
    float freq=tone==0?880.0f:tone==1?660.0f:second?1046.5f:784.0f;
    /* Original short sine chimes, no sampled proprietary phone sounds. */
    return (int16_t)(4200.0f*envelope*sinf(6.283185307f*freq*(float)index/HUB_SOUND_RATE));
}
