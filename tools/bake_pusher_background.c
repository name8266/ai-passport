/* Exact, lossless scanline runs for the fixed cabinet color/depth surfaces. */
#include "pusher_render.h"
#include "pusher_background.h"
#include <stdio.h>
#include <limits.h>
static uint16_t rgb[240*CP_TILE_H],depth[240*CP_TILE_H];
static cp_bg_header_t header={.magic=CP_BG_MAGIC,.width=240,.height=320};
static cp_bg_run_t records[240*320];
typedef struct {int32_t step,lo,hi;} candidate_t;
static candidate_t candidates[511];
static unsigned encode(const uint16_t *c,const uint16_t *d,unsigned count,unsigned *n){
    unsigned pos=0;
    while(pos<count){
        unsigned length=1;int32_t step=0,start=d[pos]*256;
        if(pos+1<count&&c[pos+1]==c[pos]){
            int32_t initial=((int32_t)d[pos+1]-d[pos])*256-255;
            for(unsigned k=0;k<511;++k)candidates[k]=(candidate_t){initial+(int32_t)k,d[pos]*256,d[pos]*256+255};
            for(unsigned i=1;pos+i<count&&c[pos+i]==c[pos];++i){
                bool found=false;
                for(unsigned k=0;k<511;++k){candidate_t *p=&candidates[k];if(p->lo>p->hi)continue;
                    int32_t lo=d[pos+i]*256-p->step*(int32_t)i,hi=lo+255;
                    if(lo>p->lo)p->lo=lo;if(hi<p->hi)p->hi=hi;
                    if(p->lo<=p->hi&&!found){found=true;step=p->step;start=p->lo;length=i+1;}}
                if(!found)break;
            }
        }
        records[(*n)++]=(cp_bg_run_t){(uint16_t)length,c[pos],(uint32_t)start,step};pos+=length;
    }
    return pos;
}
int main(int argc,char **argv){
    if(argc!=2)return 1;unsigned n=0;
    for(int y=0;y<320;y+=CP_TILE_H){cp_render_background_tile(y,rgb,depth);
        for(unsigned row=0;row<CP_TILE_H;++row){header.offset[y+row]=n;encode(rgb+row*240,depth+row*240,240,&n);}}
    header.offset[320]=n;FILE *f=fopen(argv[1],"wb");if(!f)return 1;
    if(fwrite(&header,sizeof(header),1,f)!=1||fwrite(records,sizeof(cp_bg_run_t),n,f)!=n)return 1;
    fprintf(stderr,"Cabinet: %u exact depth runs, %lu bytes\n",n,(unsigned long)(sizeof(header)+n*sizeof(cp_bg_run_t)));
    return fclose(f)!=0;
}
