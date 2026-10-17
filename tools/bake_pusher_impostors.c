/* Bake original 3D coin geometry/shading; physics remains runtime 3D. */
#include "pusher_render.h"
#include <stdio.h>
#include <math.h>
static uint16_t rgb[400];
static int16_t depth[400];
static void word(FILE *f,uint16_t v){fputc(v&255,f);fputc(v>>8,f);}
int main(int argc,char **argv){
    if(argc!=2)return 1;FILE *f=fopen(argv[1],"wb");if(!f)return 1;
    const float pi=3.14159265358979323846f;
    unsigned count=0;
    for(unsigned tilt=0;tilt<7;++tilt)for(unsigned az=0;az<(tilt?16u:1u);++az)for(unsigned spin=0;spin<16;++spin){
        float theta=tilt*pi/12,phi=az*2*pi/16,s=spin*2*pi/16;
        cp_q4 q={cosf(theta/2)*cosf((phi+s)/2),sinf(theta/2)*sinf((s-phi)/2),
                 sinf(theta/2)*cosf((phi-s)/2),cosf(theta/2)*sinf((phi+s)/2)};
        cp_render_bake_coin(q,rgb,depth);
        for(unsigned k=0;k<400;++k)word(f,rgb[k]);
        for(unsigned k=0;k<400;++k)word(f,(uint16_t)depth[k]);
        ++count;
    }
    if(fclose(f))return 1;fprintf(stderr,"Baked %u native 20x20 coin poses\n",count);return 0;
}
