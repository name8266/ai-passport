#include "pusher_render.h"
#include "pusher_coin_impostors.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static cp_game_t g;
static uint16_t *borrowed_depth;
static uint16_t last_frame[240*320],full_frame[240*320],area_frame[240*320];
static uint16_t baked_rgb[240*CP_TILE_H],baked_depth[240*CP_TILE_H],wire_pixels[240*CP_TILE_H];
static uint16_t pixels[CP_SCREEN_W*CP_TILE_H+2],depths[CP_SCREEN_W*CP_TILE_H+2];
static void orientation_equivalence(void){
 uint32_t rng=20278729;
 for(unsigned i=0;i<1000000;++i){int n[3];for(unsigned k=0;k<3;++k){rng=rng*1664525u+1013904223u;n[k]=(int)(rng%8193)-4096;}if(n[2]<0)n[2]=-n[2];
  unsigned best_id=0;int best=INT32_MIN;
  for(unsigned k=0;k<CP_ATLAS_NORMALS;++k){int score=n[0]*cp_atlas_basis[k].n[0]+n[1]*cp_atlas_basis[k].n[1]+n[2]*cp_atlas_basis[k].n[2];if(score>best){best=score;best_id=k;}}
  assert(cp_render_normal_probe(n[0],n[1],n[2])==best_id);
 }
}
static void atlas_depth_checks(void){
    static uint16_t reference[400];static int16_t relative[400];
    const float pi=3.14159265358979323846f;
    for(unsigned sample=0;sample<8;++sample){
        float theta=(sample%7)*pi/12,phi=(sample*3%16)*2*pi/16,spin=(sample*5%16)*2*pi/16;
        cp_q4 q={cosf(theta/2)*cosf((phi+spin)/2),sinf(theta/2)*sinf((spin-phi)/2),sinf(theta/2)*cosf((phi-spin)/2),cosf(theta/2)*sinf((phi+spin)/2)};
        cp_render_bake_coin(q,reference,relative);
        cp_init(&g,123,120,0,1);memset(g.coins,0,sizeof(g.coins));cp_spawn(&g,CP_COIN_GOLD,cp_v(104,110,20),q);cp_render_prepare(&g);
        int cx,cy;cp_render_project_mm(104,110,20,&cx,&cy);
        int center=60000-((110*256*3072+20*256*2703)>>16);unsigned compared=0,error=0;
        for(int y0=(cy-10)/CP_TILE_H*CP_TILE_H;y0<=cy+9;y0+=CP_TILE_H){cp_render_tile(&g,y0,pixels+1,depths+1,82);
            for(int y=0;y<20;++y){int yy=cy-10+y;if(yy<y0||yy>=y0+CP_TILE_H)continue;
                for(int x=0;x<20;++x){unsigned at=(unsigned)y*20+(unsigned)x;if(relative[at]==32767)continue;
                    unsigned screen=(unsigned)(yy-y0)*240+(unsigned)(cx-10+x);int delta=(int)depths[1+screen]-center;
                    assert(abs(delta-relative[at])<=2);
                    uint16_t a=reference[at],b=pixels[1+screen];
                    error+=(unsigned)(abs((int)(a>>11)-(int)(b>>11))*255/31+abs((int)((a>>5)&63)-(int)((b>>5)&63))*255/63+abs((int)(a&31)-(int)(b&31))*255/31);++compared;
                }
            }
        }
        assert(compared>20&&error<compared*36); /* Mean channel error below 12/255. */
    }
}
static void rogue_screens(void){
    cp_records_t board;cp_records_init(&board);cp_render_records(&board);
    cp_init(&g,442,30,0,1);cp_rogue_start(&g,0);
    for(unsigned frame=0;frame<500;++frame){
        if(frame==1)cp_rogue_key(&g,1,false);
        if(frame==2)cp_rogue_choose(&g,1);
        if(frame==3){g.autoplay=true;cp_spawn(&g,CP_CHIP,cp_v(104,116,50),(cp_q4){1,0,0,0});}
        if(frame==20){for(unsigned k=0;k<CP_RELIC_COUNT;++k)cp_rogue_grant(&g,k);cp_rogue_reel(&g);cp_reward_event(&g,CP_EVENT_SHAKE);}
        if(frame==70)cp_rogue_key(&g,2,true);
        if(frame==71){cp_rogue_key(&g,1,false);cp_rogue_key(&g,2,false);}
        if(frame>=72&&frame<101)cp_rogue_key(&g,1,false);
        if(frame==101)cp_rogue_key(&g,2,false);
        if(frame==102){g.score=75;cp_rogue_view(&g,CP_VIEW_SHOP);}
        if(frame==103)cp_rogue_key(&g,2,false);
        if(frame==104)cp_rogue_key(&g,1,false);
        if(frame==105)cp_rogue_key(&g,2,false);
        if(frame==106)cp_rogue_finish(&g);
        if(frame==107){assert(cp_records_add(&board,&g));cp_rogue_key(&g,0,false);}
        if(frame>=108&&frame<118)cp_rogue_key(&g,1,false);
        if(frame==118)cp_rogue_key(&g,2,false);
        if(frame>=119&&frame<148)cp_rogue_key(&g,1,false);
        if(frame==148)cp_rogue_key(&g,2,false);
        if(frame==149)cp_rogue_key(&g,2,true);
        if(frame==150){cp_init(&g,1929,30,0,1);cp_rogue_start(&g,board.discovered);}
        if(frame==151)cp_rogue_choose(&g,0);
        if(frame==151){cp_rogue_key(&g,1,false);}
        if(frame>=153&&frame<=160){cp_rogue_key(&g,1,false);}
        if(frame==161){cp_rogue_key(&g,2,true);(void)cp_tactics_cast(&g,CP_SP_PRISM);}
        if(frame==162){for(unsigned k=0;k<CP_MAX_COINS;++k)if(g.coins[k].active&&g.coins[k].kind==CP_COIN_GOLD)g.coins[k].trait|=CP_TRAIT_DOUBLE;}
        if(frame==170){g.rogue.pending=0;g.rogue.gross=cp_tactics_goal(&g);g.collected=cp_tactics_quota(&g);cp_tactics_tick(&g);}
        if(frame==171){cp_rogue_key(&g,1,false);}
        if(frame==172){cp_rogue_key(&g,2,false);}
        if(frame==180){cp_rogue_finish(&g);}
        if(frame==181){cp_init(&g,1929,30,0,1);cp_rogue_start(&g,0);cp_rogue_choose(&g,0);}
        if(frame==182){g.bank=0;g.rogue.grace=599;}
        cp_step(&g,frame%37?CP_DT:CP_DT*2);cp_render_prepare(&g);uint16_t dirty=frame?cp_render_dirty():UINT16_MAX;
        for(int y=0;y<320;y+=CP_TILE_H){
            cp_render_tile(&g,y,pixels+1,depths+1,82);memcpy(full_frame+y*240,pixels+1,240*CP_TILE_H*2);
            if(dirty&(1u<<(y/CP_TILE_H))){int x0,x1;cp_render_region(y,&x0,&x1);if(!frame){x0=0;x1=240;}
                for(int row=0;row<CP_TILE_H;++row)memcpy(last_frame+(y+row)*240+x0,pixels+1+row*240+x0,(unsigned)(x1-x0)*2);
                int ay0,ay1,ax0,ax1;cp_render_area(y,&ax0,&ay0,&ax1,&ay1);assert(ay0>=y&&ay1<=y+CP_TILE_H&&ay1>ay0);
                memset(wire_pixels,0x6b,sizeof(wire_pixels));cp_render_spi_area(&g,ay0,ay1-ay0,wire_pixels,borrowed_depth,82,ax0,ax1);
                for(unsigned q=(unsigned)(ay1-ay0)*240;q<240*CP_TILE_H;++q)assert(wire_pixels[q]==0x6b6b);
                for(int yy=ay0;yy<ay1;++yy)memcpy(area_frame+yy*240+ax0,pixels+1+(yy-y)*240+ax0,(unsigned)(ax1-ax0)*2);
                for(int yy=ay0;yy<ay1;++yy)for(int x=ax0;x<ax1;++x){uint16_t native=pixels[1+(yy-y)*240+x];assert(wire_pixels[(yy-ay0)*240+x]==(uint16_t)((native<<8)|(native>>8)));}
            }
        }
        if(memcmp(area_frame,full_frame,sizeof(full_frame))){for(unsigned at=0;at<240*320;++at)if(area_frame[at]!=full_frame[at]){fprintf(stderr,"Area mismatch frame%u at%u,%u old%04x new%04x\n",frame,at%240,at/240,area_frame[at],full_frame[at]);break;}assert(0);}
        if(memcmp(last_frame,full_frame,sizeof(full_frame))){for(unsigned q=0;q<240*320;++q)if(last_frame[q]!=full_frame[q]){fprintf(stderr,"Rogue dirty mismatch frame %u view%u at %u,%u old%04x new%04x mask%04x\n",frame,g.rogue.view,q%240,q/240,last_frame[q],full_frame[q],dirty);break;}assert(0);}
    }
    cp_render_records(NULL);
}

int main(int argc,char **argv)
{
    borrowed_depth=cp_frame_depth_scratch(240*CP_TILE_H*2);
    assert(borrowed_depth);
    assert(!cp_frame_depth_scratch(1));
    assert(cp_render_assets_ready());orientation_equivalence();
#ifndef CP_RENDER_COMPACT
    atlas_depth_checks();
#else
    (void)atlas_depth_checks; /* Compact target uses atlas coins; mesh baking remains a host-only tool. */
#endif
    int lx,ly,rx,ry,tx,ty;
    cp_render_project_mm(20,70,20,&lx,&ly);cp_render_project_mm(188,70,20,&rx,&ry);
    assert(lx+rx==240&&ly==ry); /* no camera yaw or screen roll */
    cp_render_project_mm(20,70,140,&tx,&ty);assert(tx==lx&&ty<ly);
    _Alignas(4) uint16_t packed[240*CP_TILE_H+2];
    for(unsigned offset=0;offset<2;++offset)for(int x0=0;x0<=24;x0+=8)for(int x1=160;x1<=240;x1+=40){
        for(unsigned i=0;i<240*CP_TILE_H;++i)packed[offset+i]=(uint16_t)i;
        cp_render_pack_region(packed+offset,x0,x1);
        for(unsigned row=0;row<CP_TILE_H;++row)for(unsigned x=0;x<(unsigned)(x1-x0);++x)assert(packed[offset+row*(unsigned)(x1-x0)+x]==row*240+(unsigned)x0+x);
    }
    cp_init(&g,123,120,0,1);
    if(argc>2){g.autoplay=true;for(unsigned i=0;i<atoi(argv[2])*30u;++i){cp_step(&g,1.0f/30);}}
    for(int y=0;y<320;y+=CP_TILE_H){cp_render_background_tile(y,pixels+1,depths+1);
        assert(cp_render_base_tile(y,baked_rgb,baked_depth));
        assert(memcmp(baked_rgb,pixels+1,sizeof(baked_rgb))==0&&memcmp(baked_depth,depths+1,sizeof(baked_depth))==0);}
    cp_render_prepare(&g);FILE *out=argc>1?fopen(argv[1],"wb"):NULL;
    if(out)fprintf(out,"P6\n240 320\n255\n");
    unsigned nonblack=0;
    for(int y=0;y<320;y+=CP_TILE_H){
        pixels[0]=pixels[CP_SCREEN_W*CP_TILE_H+1]=0xabcd;
        depths[0]=depths[CP_SCREEN_W*CP_TILE_H+1]=0xdcba;
        cp_render_tile(&g,y,pixels+1,depths+1,82);
        assert(pixels[0]==0xabcd&&pixels[CP_SCREEN_W*CP_TILE_H+1]==0xabcd);
        assert(depths[0]==0xdcba&&depths[CP_SCREEN_W*CP_TILE_H+1]==0xdcba);
        cp_render_spi_tile(&g,y,wire_pixels,borrowed_depth,82);
        for(unsigned i=0;i<240*CP_TILE_H;++i)assert(wire_pixels[i]==(uint16_t)((pixels[i+1]<<8)|(pixels[i+1]>>8)));

        memcpy(full_frame+y*240,pixels+1,240*CP_TILE_H*2);
        for(unsigned i=1;i<=CP_SCREEN_W*CP_TILE_H;++i){uint16_t c=pixels[i];nonblack+=c!=0;
            if(out){unsigned char rgb[3]={(unsigned char)((c>>11)*255/31),(unsigned char)(((c>>5)&63)*255/63),(unsigned char)((c&31)*255/31)};fwrite(rgb,1,3,out);}}
    }
    if(out)fclose(out);assert(nonblack>70000);
    /* Native scene pixels must retain detail within 2x2 cells. The former
     * nearest-neighbor enlarged scene fails this test even with native HUD. */
    unsigned native_detail=0;
    for(unsigned y=64;y<256;y+=2)for(unsigned x=24;x<216;x+=2){
        unsigned at=y*240+x;uint16_t a=full_frame[at];
        if(a!=full_frame[at+1]||a!=full_frame[at+240]||a!=full_frame[at+241])++native_detail;
    }
    assert(native_detail>100);
    memcpy(area_frame,full_frame,sizeof(full_frame));
    cp_init(&g,321,200,0,1);g.autoplay=true;
    for(unsigned frame=0;frame<1800;++frame){
        if(frame%180==60)cp_reward_event(&g,(cp_event_t)(1+(frame/180)%CP_EVENT_MAX));
        if(frame==30)g.paused=true;if(frame==31)g.paused=false;
        cp_step(&g,frame%37?CP_DT:CP_DT*2);cp_render_prepare(&g);uint16_t dirty=frame?cp_render_dirty():UINT16_MAX;
        for(int y=0;y<320;y+=CP_TILE_H){cp_render_tile(&g,y,pixels+1,depths+1,82);
            memcpy(full_frame+y*240,pixels+1,240*CP_TILE_H*2);
            if(dirty&(1u<<(y/CP_TILE_H))){int x0,x1;cp_render_region(y,&x0,&x1);if(!frame){x0=0;x1=240;}
                for(int row=0;row<CP_TILE_H;++row)memcpy(last_frame+(y+row)*240+x0,pixels+1+row*240+x0,(unsigned)(x1-x0)*2);
                int ay0,ay1,ax0,ax1;cp_render_area(y,&ax0,&ay0,&ax1,&ay1);assert(ay0>=y&&ay1<=y+CP_TILE_H&&ay1>ay0);
                memset(wire_pixels,0x6b,sizeof(wire_pixels));cp_render_spi_area(&g,ay0,ay1-ay0,wire_pixels,borrowed_depth,82,ax0,ax1);
                for(unsigned q=(unsigned)(ay1-ay0)*240;q<240*CP_TILE_H;++q)assert(wire_pixels[q]==0x6b6b);
                for(int yy=ay0;yy<ay1;++yy)memcpy(area_frame+yy*240+ax0,pixels+1+(yy-y)*240+ax0,(unsigned)(ax1-ax0)*2);
                for(int yy=ay0;yy<ay1;++yy)for(int x=ax0;x<ax1;++x){uint16_t native=pixels[1+(yy-y)*240+x];assert(wire_pixels[(yy-ay0)*240+x]==(uint16_t)((native<<8)|(native>>8)));}
            }}
        if(memcmp(last_frame,full_frame,sizeof(full_frame))){for(unsigned at=0;at<240*320;++at)if(last_frame[at]!=full_frame[at]){fprintf(stderr,"Arcade dirty mismatch frame%u at%u,%u old%04x new%04x\n",frame,at%240,at/240,last_frame[at],full_frame[at]);break;}assert(0);}if(memcmp(area_frame,full_frame,sizeof(full_frame))){for(unsigned at=0;at<240*320;++at)if(area_frame[at]!=full_frame[at]){fprintf(stderr,"Area mismatch frame%u at%u,%u old%04x new%04x\n",frame,at%240,at/240,area_frame[at],full_frame[at]);break;}assert(0);}
    }
    rogue_screens();
    puts("3D tiled depth renderer + all Roguelike screens: PASS");
}
