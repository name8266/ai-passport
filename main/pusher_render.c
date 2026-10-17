#include "pusher_render.h"
#include "pusher_background.h"
#include "pusher_photo.h"
#ifdef ESP_PLATFORM
#include "esp_attr.h"
#include "esp_cpu.h"
#define CP_DRAW_HOT IRAM_ATTR __attribute__((noinline))
#define CP_NOIPA __attribute__((noipa))
#else
#define CP_DRAW_HOT
#define CP_NOIPA
#endif
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#ifndef CP_PHOTO_RGB
#define CP_PHOTO_RGB 0
#endif
#include "pusher_materials.h"
#include "pusher_font.h"
#ifndef CP_RENDER_BAKE_COIN
#include "pusher_coin_impostors.h"
#endif
#ifdef CP_RENDER_HOST
#include <stdlib.h>
static bool s_baking_coin;
#else
#define s_baking_coin false
#endif

static cp_render_profile_t s_profile;
cp_render_profile_t cp_render_profile(void){return s_profile;}
static inline uint32_t draw_cycles(void){
#ifdef ESP_PLATFORM
    return esp_cpu_get_cycle_count();
#else
    return 0;
#endif
}
typedef struct { int16_t x,y;uint16_t z; } point_t;
#if defined(ESP_PLATFORM) || defined(CP_RENDER_COMPACT)
#define CP_MESH_POINTS 8
#else
#define CP_MESH_POINTS 16
#endif
/* Ordinary coins need only a projected center and depth-atlas selector.
 * Prize polygons live in the exclusive frame arena, not in every coin slot. */
typedef struct {
    point_t center;
    int16_t position[3],orientation[4];
    int16_t ymin,ymax,xmin,xmax;
    uint16_t atlas,shape;
    uint8_t kind;bool valid;
} mesh_t;
typedef struct {
    point_t p[CP_MESH_POINTS];uint16_t cap,side[8],bottom;
    uint8_t visible,light;bool top;
} mesh_shape_t;
static mesh_shape_t *s_shapes;
static unsigned s_shape_count;
typedef struct {
    struct {int32_t x,dx;int16_t y0,y1;} edges[8];
    int32_t zr,ur,vr,dz,du,dv,dzy,duy,dvy;
    int16_t xmin,xmax,ymin,ymax;
    uint16_t generation;uint8_t edge_count;bool valid;
} polygon_cache_t;
static unsigned s_polygon_count;
typedef struct {int16_t x,y;uint16_t depth;uint8_t radius,pad;} shadow_t;
static shadow_t *s_shadows;
typedef struct {uint16_t photo[256],atlas[32],gold[128],red[128],blue[128],green[128],sides[3][16],steel[16],rows[32];
#ifndef CP_RENDER_BAKE_COIN
 cp_atlas_basis_t basis[CP_ATLAS_NORMALS];int16_t ccos[16],ssin[16];
#endif
} color_cache_t;
static color_cache_t *s_colors;
static uint16_t *s_photo_palette;
static unsigned s_shadow_count;
static polygon_cache_t *s_polygons;
static uint16_t s_generation;
static int s_polygon_slot=-1;
static mesh_t s_mesh[CP_MAX_COINS];
static uint16_t s_order[CP_MAX_COINS];
static uint16_t s_near[CP_MAX_COINS];
static unsigned s_order_count;
static const uint16_t *s_material;
static int s_slab_texture=-1;
static unsigned s_reflection_phase;
static const uint8_t *texture_map(int texture){
    switch(texture){case 0:return cp_metal_copper;case 1:return cp_metal_gold;
    case 2:return cp_tex_red;case 3:return cp_tex_blue;case 4:return cp_tex_green;default:return cp_tex_steel;}
}
/* The photographed cabinet supplies color; physics-derived surfaces supply depth.
 * Pegs and inlet remain explicit geometry at their actual collision positions. */
static bool cabinet_detail(uint16_t c){return c==0x9556||c==0xe75b||c==0xdd48;}
#ifdef CP_RENDER_HOST
static uint16_t cabinet_pixel(unsigned x,unsigned y,uint16_t geometry){
    return cabinet_detail(geometry)?geometry:cp_cabinet_pixels[y*240+x];
}
#endif
static uint16_t *s_rgb,*s_depth;
static int s_y0;
static int s_width=240,s_height=320,s_tile_h=CP_TILE_H,s_requested_h=CP_TILE_H;
static bool s_half,s_ui_spi;
static int s_native_x0=0,s_native_x1=240,s_clip_x0=0,s_clip_x1=240;
static void scene_context(bool half){s_half=half;s_ui_spi=false;s_width=half?120:240;s_height=half?160:320;s_tile_h=half?s_requested_h/2:s_requested_h;s_clip_x0=half?s_native_x0/2:s_native_x0;s_clip_x1=half?s_native_x1/2:s_native_x1;}
static uint16_t wire_color(uint16_t c){return (uint16_t)((c<<8)|(c>>8));}
static uint16_t s_dirty_tiles;
static struct {uint32_t bank,score,prizes,game_ui,effects,jp_step,light_phase;unsigned tower,remaining;uint8_t progress,mask,tower_side,blink;bool guard,jackpot,circulation;float pusher;bool paused,autoplay,payout,full,valid;} s_previous;
static int16_t s_dirty_x0[16],s_dirty_x1[16],s_dirty_y0[16],s_dirty_y1[16];
static CP_DRAW_HOT void dirty_rect(int x0,int y0,int x1,int y1){
    if(y1<0||y0>=320||x1<0||x0>=240)return;
    if(y0<0)y0=0;
    if(y1>319)y1=319;
    if(x0<0)x0=0;
    if(x1>239)x1=239;
    x0&=~1;x1=(x1+2)&~1;
    for(int i=y0/CP_TILE_H;i<=y1/CP_TILE_H;++i){s_dirty_tiles|=(uint16_t)(1u<<i);if(x0<s_dirty_x0[i])s_dirty_x0[i]=x0;if(x1>s_dirty_x1[i])s_dirty_x1[i]=x1;int lo=y0>i*CP_TILE_H?y0:i*CP_TILE_H,hi=y1+1<(i+1)*CP_TILE_H?y1+1:(i+1)*CP_TILE_H;if(lo<s_dirty_y0[i])s_dirty_y0[i]=lo;if(hi>s_dirty_y1[i])s_dirty_y1[i]=hi;}
}
static void dirty_y(int y0,int y1){dirty_rect(0,y0,239,y1);}
uint16_t cp_render_dirty(void){return s_dirty_tiles;}
void cp_render_region(int y0,int *x0,int *x1){unsigned tile=(unsigned)y0/CP_TILE_H;*x0=s_dirty_x0[tile];*x1=s_dirty_x1[tile];if(*x1<=*x0){*x0=0;*x1=240;}}
void cp_render_area(int tile_y,int *x0,int *y0,int *x1,int *y1){
 unsigned tile=(unsigned)tile_y/CP_TILE_H;cp_render_region(tile_y,x0,x1);*y0=s_dirty_y0[tile];*y1=s_dirty_y1[tile];
 if(*y1<=*y0){*y0=tile_y;*y1=tile_y+CP_TILE_H;}
}
typedef struct {int32_t x,y,z;} rv3;
typedef struct {rv3 p,a[3],half;} body_mesh_t;
static unsigned s_tower,s_remaining;
static cp_rod_t s_render_rod;
static const int16_t rx[8]={4096,2896,0,-2896,-4096,-2896,0,2896};
static const int16_t ry[8]={0,2896,4096,2896,0,-2896,-4096,-2896};
static uint16_t color(unsigned r,unsigned g,unsigned b) {uint16_t c=(uint16_t)(((r>>3)<<11)|((g>>2)<<5)|(b>>3));return s_ui_spi?wire_color(c):c;}
static uint16_t shade(uint16_t c,int32_t f){
    unsigned r=((c>>11)*f)>>12,g=(((c>>5)&63)*f)>>12,b=((c&31)*f)>>12;
    if(r>31)r=31;
    if(g>63)g=63;
    if(b>31){b=31;}return (uint16_t)((r<<11)|(g<<5)|b);
}
static int32_t rdot(rv3 a,rv3 b){return (a.x*b.x+a.y*b.y+a.z*b.z)>>12;}
static point_t project(rv3 p){
    /* Centered front elevation. Horizontal rows remain horizontal and
     * vertical posts remain vertical; orthographic depth still resolves 3D. */
    int sx=120+((p.x-104*256+128)>>8);
    int sy=176+((2949*(p.y-18*256)-3359*p.z+524288)>>20);
    int z=60000-((p.y*3072+p.z*2703)>>16);
    if(z<1){z=1;}
    if(z>65535){z=65535;}
    return (point_t){(int16_t)(s_half?sx/2:sx),(int16_t)(s_half?sy/2:sy),(uint16_t)z};
}
void cp_render_project_mm(int x,int y,int z,int *sx,int *sy){
    scene_context(false);point_t p=project((rv3){x*256,y*256,z*256});*sx=p.x;*sy=p.y;
}
static rv3 transform(const body_mesh_t *b,int32_t x,int32_t y,int32_t z){
    return (rv3){b->p.x+((b->a[0].x*x+b->a[1].x*y+b->a[2].x*z)>>12),
        b->p.y+((b->a[0].y*x+b->a[1].y*y+b->a[2].y*z)>>12),b->p.z+((b->a[0].z*x+b->a[1].z*y+b->a[2].z*z)>>12)};
}
static rv3 rm(rv3 a,int32_t t){return (rv3){(a.x*t)>>12,(a.y*t)>>12,(a.z*t)>>12};}
static rv3 ra(rv3 a,rv3 b){return (rv3){a.x+b.x,a.y+b.y,a.z+b.z};}
#ifndef CP_RENDER_BAKE_COIN
static CP_DRAW_HOT unsigned coin_normal_id(rv3 normal){
    /* All six latitude rings share sixteen azimuths. Only the closest three
     * azimuths can win; compare their actual integer normals to retain ties. */
    const int16_t *ccos=s_colors?s_colors->ccos:cp_atlas_cos,*ssin=s_colors?s_colors->ssin:cp_atlas_sin;
    const cp_atlas_basis_t *basis=s_colors?s_colors->basis:cp_atlas_basis;
    unsigned az[3]={0,0,0};int32_t score[3]={INT32_MIN,INT32_MIN,INT32_MIN};
    for(unsigned k=0;k<16;++k){int32_t q=normal.x*ccos[k]+normal.y*ssin[k];
        for(unsigned j=0;j<3;++j)if(q>score[j]){for(unsigned m=2;m>j;--m){score[m]=score[m-1];az[m]=az[m-1];}score[j]=q;az[j]=k;break;}}
    unsigned id=0;int32_t best=normal.z*4096;
    for(unsigned ring=0;ring<6;++ring)for(unsigned j=0;j<3;++j){unsigned k=1+ring*16+az[j];const cp_atlas_basis_t *b=&basis[k];
        int32_t q=normal.x*b->n[0]+normal.y*b->n[1]+normal.z*b->n[2];if(q>best||(q==best&&k<id)){best=q;id=k;}}
    return id;
}
#ifdef CP_RENDER_HOST
unsigned cp_render_normal_probe(int x,int y,int z){return coin_normal_id((rv3){x,y,z});}
#endif
static CP_DRAW_HOT unsigned coin_orientation(const body_mesh_t *body){
    rv3 normal=body->a[2];if(normal.z<0)normal=rm(normal,-4096);
    int32_t best;unsigned normal_id=coin_normal_id(normal);
    const cp_atlas_basis_t *basis=s_colors?&s_colors->basis[normal_id]:&cp_atlas_basis[normal_id];rv3 axis=body->a[0];
    const int16_t *ccos=s_colors?s_colors->ccos:cp_atlas_cos,*ssin=s_colors?s_colors->ssin:cp_atlas_sin;
    int32_t u=(axis.x*basis->u[0]+axis.y*basis->u[1]+axis.z*basis->u[2])>>12;
    int32_t v=(axis.x*basis->v[0]+axis.y*basis->v[1]+axis.z*basis->v[2])>>12;
    unsigned spin=0;best=INT32_MIN;
    for(unsigned k=0;k<16;++k){int32_t score=u*ccos[k]+v*ssin[k];if(score>best){best=score;spin=k;}}
    return normal_id*16+spin;
}
#endif
static uint8_t s_marks[CP_MAX_COINS];
static void rogue_dirty(const cp_game_t *g);
__attribute__((noinline)) void cp_render_prepare(const cp_game_t *g){
    scene_context(false);memset(&s_profile,0,sizeof(s_profile));
    unsigned shapes=0;
    for(unsigned i=0;i<CP_MAX_COINS;++i){
#ifdef CP_RENDER_BAKE_COIN
        shapes+=g->coins[i].active;
#else
        shapes+=g->coins[i].active&&(cp_kind_prize(g->coins[i].kind)||s_baking_coin);
#endif
    }
    s_shape_count=0;
    unsigned base_size=sizeof(shadow_t)*CP_MAX_COINS+sizeof(color_cache_t)+sizeof(mesh_shape_t)*shapes;
    base_size=(base_size+3u)&~3u;s_polygon_count=shapes+70;
    unsigned char *arena=cp_frame_scratch(base_size+sizeof(polygon_cache_t)*s_polygon_count);
    if(!arena){arena=cp_frame_scratch(base_size);s_polygon_count=0;}
    s_colors=(color_cache_t *)arena;
    s_shadows=s_colors?(shadow_t *)(s_colors+1):NULL;s_shadow_count=0;
    s_shapes=s_shadows?(mesh_shape_t *)(s_shadows+CP_MAX_COINS):NULL;
    s_polygons=arena&&s_polygon_count?(polygon_cache_t *)(arena+base_size):NULL;
    s_photo_palette=s_colors?s_colors->photo:NULL;
    if(s_colors){
        memcpy(s_colors->photo,cp_photo_palette,sizeof(s_colors->photo));
#ifndef CP_RENDER_BAKE_COIN
        memcpy(s_colors->atlas,cp_atlas_palette,sizeof(s_colors->atlas));
        memcpy(s_colors->basis,cp_atlas_basis,sizeof(s_colors->basis));memcpy(s_colors->ccos,cp_atlas_cos,sizeof(s_colors->ccos));memcpy(s_colors->ssin,cp_atlas_sin,sizeof(s_colors->ssin));
#endif
        memcpy(s_colors->gold,cp_lit_gold,sizeof(s_colors->gold));memcpy(s_colors->red,cp_lit_red,sizeof(s_colors->red));
        memcpy(s_colors->blue,cp_lit_blue,sizeof(s_colors->blue));memcpy(s_colors->green,cp_lit_green,sizeof(s_colors->green));
        memcpy(s_colors->sides[0],cp_palette_red_side,32);memcpy(s_colors->sides[1],cp_palette_blue_side,32);memcpy(s_colors->sides[2],cp_palette_green_side,32);
        memcpy(s_colors->steel,cp_palette_steel,32);memcpy(s_colors->rows,cp_steel_rows,64);
    }
    /* Contacts cease to be live after solve. Every frame recreates its cache. */
    if(s_polygons){for(unsigned i=0;i<s_polygon_count;++i)s_polygons[i].generation=0;}s_generation=1;
    s_dirty_tiles=0;for(unsigned i=0;i<16;++i){s_dirty_x0[i]=240;s_dirty_x1[i]=0;s_dirty_y0[i]=(int16_t)((i+1)*CP_TILE_H);s_dirty_y1[i]=(int16_t)(i*CP_TILE_H);}s_tower=cp_tower_count(g);s_remaining=cp_prize_count(g);
    rogue_dirty(g);
    if(s_previous.light_phase!=g->steps/3){dirty_rect(20,191,221,199);dirty_rect(14,207,18,272);dirty_rect(221,207,225,272);}
    s_previous.light_phase=g->steps/3;
    if(!s_previous.valid)dirty_y(0,319);
    if(s_previous.bank!=g->bank||s_previous.score!=g->score)dirty_y(0,53);
    if(s_previous.remaining!=s_remaining||s_previous.prizes!=g->prizes||s_previous.tower!=s_tower||s_previous.autoplay!=g->autoplay||s_previous.paused!=g->paused)dirty_y(274,319);
    uint32_t game_ui=2166136261u;
    const uint32_t fields[]={(g->rush_ticks+29)/30,(g->guard_ticks+29)/30,g->bonus_queue,g->event_kind,(g->event_ticks+29)/30,g->restocks,g->arcade.lane,g->arcade.spin_ticks?(45-g->arcade.spin_ticks)/5:0,g->arcade.cooldown/30,(g->arcade.double_ticks+29)/30,(g->arcade.fever_ticks+29)/30,(g->arcade.hole_ticks+29)/30,(g->arcade.shake_ticks+29)/30,g->arcade.tower_ticks>0,g->arcade.spin_ticks>0,g->arcade.reveal_ticks>0};
    for(unsigned k=0;k<sizeof(fields)/sizeof(fields[0]);++k)game_ui=(game_ui^fields[k])*16777619u;
    if(s_previous.game_ui!=game_ui){dirty_y(274,319);dirty_y(32,53);dirty_rect(20,191,221,199);}
    if(s_previous.progress!=g->arcade.lane||s_previous.mask!=g->collection_mask)dirty_y(32,53);
    if(g->circulation!=s_previous.circulation)dirty_y(222,274);
    if(g->circulation&&s_previous.blink!=((g->steps/10)&1))dirty_rect(18,254,28,269);
    s_previous.blink=(uint8_t)((g->steps/10)&1);
    if(g->circulation!=s_previous.circulation)dirty_y(274,319);
    s_previous.circulation=g->circulation;
    uint32_t effects=(g->arcade.hole_ticks?1u:0)|(g->arcade.tower_ticks?2u:0)|((g->arcade.spin_ticks||g->arcade.reveal_ticks)?4u:0)|(g->arcade.fever_ticks?8u:0);
    if((effects|s_previous.effects)&1u)dirty_rect(83,218,157,274);
    if((effects|s_previous.effects)&2u){
        if(effects&2u)dirty_rect(g->arcade.tower_side?162:0,194,g->arcade.tower_side?239:78,274);
        if(s_previous.effects&2u)dirty_rect(s_previous.tower_side?162:0,194,s_previous.tower_side?239:78,274);
    }
    s_previous.tower_side=g->arcade.tower_side;
    if((effects|s_previous.effects)&4u)dirty_rect(44,91,196,145);
    if((effects^s_previous.effects)&8u)dirty_rect(18,218,222,274);
    /* Moving lane lamps sit at the physical guide exit. */
    if(s_previous.progress!=g->arcade.lane||s_previous.game_ui!=game_ui)dirty_rect(46,175,194,203);
    s_previous.effects=effects;
    bool guard=g->guard_ticks>0,jackpot=g->event_kind==CP_EVENT_JACKPOT&&g->event_ticks>2*CP_HZ;
    if(guard!=s_previous.guard)dirty_y(236,274);
    if(jackpot!=s_previous.jackpot)dirty_y(53,274);
    else if(jackpot){
        for(unsigned k=0;k<10;++k){
            int x=28+(int)((k*37+g->steps*2)%184),y=62+(int)((k*53+g->steps*3)%200);
            int old_x=28+(int)((k*37+s_previous.jp_step*2)%184),old_y=62+(int)((k*53+s_previous.jp_step*3)%200);
            dirty_rect(old_x-2,old_y-2,old_x+2,old_y+2);dirty_rect(x-2,y-2,x+2,y+2);
        }
    }
    s_previous.jp_step=g->steps;
    s_previous.guard=guard;s_previous.jackpot=jackpot;s_previous.game_ui=game_ui;s_previous.progress=g->arcade.lane;s_previous.mask=g->collection_mask;
    bool full=cp_coin_count(g)>=CP_MAX_COINS;if(s_previous.full!=full)dirty_y(294,319);s_previous.full=full;
    if(s_previous.paused!=g->paused)dirty_y(146,177);
    if(s_previous.payout!=(g->payout_flash>0))dirty_y(254,270);
    cp_rod_t rod;cp_game_rod(g,g->game_time,&rod);s_render_rod=rod;
    static int32_t previous_rod_x,previous_rod_z;
    if(rod.x!=previous_rod_x||rod.z!=previous_rod_z){dirty_rect(76,48,164,108);previous_rod_x=rod.x;previous_rod_z=rod.z;}
    if(s_previous.pusher!=g->pusher_y){
        int front=44*256+(int32_t)(g->pusher_y*256),old=44*256+(int32_t)(s_previous.pusher*256);
        point_t p[6]={project((rv3){12*256,18*256,18*256}),project((rv3){196*256,18*256,18*256}),project((rv3){12*256,front,14*256}),project((rv3){196*256,front,14*256}),project((rv3){12*256,old,14*256}),project((rv3){196*256,old,14*256})};
        int lo=p[0].y,hi=lo,left=p[0].x,right=left;for(unsigned k=1;k<6;++k){if(p[k].y<lo)lo=p[k].y;if(p[k].y>hi)hi=p[k].y;if(p[k].x<left)left=p[k].x;if(p[k].x>right)right=p[k].x;}dirty_rect(left-3,lo-3,right+3,hi+3);
    }
    s_previous.remaining=s_remaining;s_previous.bank=g->bank;s_previous.score=g->score;s_previous.prizes=g->prizes;s_previous.tower=s_tower;s_previous.autoplay=g->autoplay;s_previous.paused=g->paused;s_previous.payout=g->payout_flash>0;s_previous.pusher=g->pusher_y;s_previous.valid=true;
    for(unsigned i=0;i<CP_MAX_COINS;++i){
        const cp_coin_t *b=&g->coins[i];mesh_t *m=&s_mesh[i];
        if(!b->active){if(m->valid)dirty_rect(m->xmin-2,m->ymin-2,m->xmax+2,m->ymax+2);m->valid=false;continue;}
        uint8_t mark=(uint8_t)(b->special|(b->trait<<4));
        if(mark!=s_marks[i]){if(m->valid)dirty_rect(m->xmin-3,m->ymin-3,m->xmax+3,m->ymax+3);s_marks[i]=mark;m->valid=false;}
        body_mesh_t fixed;cp_pose_fixed_t pose;
        if(cp_pose_fixed(g,i,&pose)){fixed.p=(rv3){pose.p[0],pose.p[1],pose.p[2]};fixed.half=(rv3){pose.half[0],pose.half[1],pose.half[2]};
            for(unsigned k=0;k<3;++k)fixed.a[k]=(rv3){pose.axis[k][0],pose.axis[k][1],pose.axis[k][2]};}
        else{pose.p[0]=(int32_t)(b->p.x*256);pose.p[1]=(int32_t)(b->p.y*256);pose.p[2]=(int32_t)(b->p.z*256);
            pose.q[0]=(int32_t)(b->q.w*32768);pose.q[1]=(int32_t)(b->q.x*32768);pose.q[2]=(int32_t)(b->q.y*32768);pose.q[3]=(int32_t)(b->q.z*32768);
            fixed.p=(rv3){pose.p[0],pose.p[1],pose.p[2]};fixed.half=(rv3){(int32_t)(cp_body_half(b).x*256),(int32_t)(cp_body_half(b).y*256),(int32_t)(cp_body_half(b).z*256)};
            for(unsigned k=0;k<3;++k)fixed.a[k]=(rv3){(int32_t)(cp_body_axis(b,k).x*4096),(int32_t)(cp_body_axis(b,k).y*4096),(int32_t)(cp_body_axis(b,k).z*4096)};}
        /* Bound pose error below one native scene pixel. Keep
         * the last displayed pose until translation/rotation accumulates;
         * fast falling/rolling bodies still rebuild every display frame. */
        if(m->valid&&m->kind==b->kind){int32_t distance=0,angle=0;
            for(unsigned k=0;k<3;++k){int32_t d=pose.p[k]-(int32_t)m->position[k]*2;distance+=d<0?-d:d;}
            for(unsigned k=0;k<4;++k){int32_t d=pose.q[k]-(int32_t)m->orientation[k]*2;angle+=d<0?-d:d;}
            if(distance<192&&angle<768&&!cp_kind_prize(b->kind)&&!s_baking_coin)continue;
        }
        if(m->valid)dirty_rect(m->xmin-2,m->ymin-2,m->xmax+2,m->ymax+2);
        for(unsigned k=0;k<3;++k){m->position[k]=(int16_t)(pose.p[k]>>1);}
        for(unsigned k=0;k<4;++k){m->orientation[k]=(int16_t)(pose.q[k]>>1);}m->kind=b->kind;m->valid=true;
#ifndef CP_RENDER_BAKE_COIN
        if(!cp_kind_prize(b->kind)&&!s_baking_coin){
            m->atlas=(uint16_t)coin_orientation(&fixed);m->center=project(fixed.p);
            const uint8_t *bounds=cp_atlas_bounds[m->atlas];
            m->xmin=m->center.x-10+bounds[0];m->ymin=m->center.y-10+bounds[1];
            m->xmax=m->center.x-10+bounds[2];m->ymax=m->center.y-10+bounds[3];
        }else
#endif
        {
        m->shape=(uint16_t)s_shape_count++;mesh_shape_t *shape=&s_shapes[m->shape];shape->visible=0;
        rv3 view={0,3072,2703},light={-1303,-1114,3719};shape->top=rdot(fixed.a[2],view)>0;
        uint16_t base=b->kind==CP_COIN_GOLD?color(255,210,75):b->kind==CP_COIN_COPPER?color(255,210,75):b->kind==CP_PRIZE_RED?color(212,49,67):b->kind==CP_PRIZE_BLUE?color(56,142,212):color(55,180,121);
        int32_t lighting=rdot(light,fixed.a[2]);
    if(lighting<0){lighting=-lighting;}shape->cap=shade(base,3072+((1024*lighting)>>12));
        shape->light=(uint8_t)(lighting>=4096?7:lighting>>9);
        #if defined(ESP_PLATFORM) || defined(CP_RENDER_COMPACT)
        unsigned n=4; /* Only prize boxes use mesh geometry; coins use the exact atlas. */
#else
        unsigned n=cp_kind_prize(b->kind)?4:8;
#endif
        for(unsigned k=0;k<n;++k){
            int32_t x=n==4?(k==0||k==3?-fixed.half.x:fixed.half.x):(rx[k]*fixed.half.x)>>12;
            int32_t y=n==4?(k<2?-fixed.half.y:fixed.half.y):(ry[k]*fixed.half.y)>>12;
            shape->p[k]=project(transform(&fixed,x,y,fixed.half.z));shape->p[k+n]=project(transform(&fixed,x,y,-fixed.half.z));
            rv3 normal;
            if(n==4)normal=k==0?rm(fixed.a[1],-4096):k==1?fixed.a[0]:k==2?fixed.a[1]:rm(fixed.a[0],-4096);
            else normal=rm(ra(rm(fixed.a[0],rx[k]+rx[(k+1)%8]),rm(fixed.a[1],ry[k]+ry[(k+1)%8])),2217);
            if(rdot(normal,view)>0)shape->visible|=(uint8_t)(1u<<k);
            lighting=rdot(normal,light);
    if(lighting<0){lighting=0;}shape->side[k]=shade(base,1556+((1843*lighting)>>12));
        }
        shape->bottom=shade(shape->cap,2253);m->center=shape->p[0];m->ymin=m->ymax=m->center.y;m->xmin=m->xmax=m->center.x;
        for(unsigned k=1;k<2*n;++k){point_t v=shape->p[k];
    if(v.y<m->ymin)m->ymin=v.y;
    if(v.y>m->ymax)m->ymax=v.y;
    if(v.x<m->xmin)m->xmin=v.x;
    if(v.x>m->xmax)m->xmax=v.x;}
        }
        if(!b->feeder&&pose.p[1]>92*256&&pose.p[2]<35*256&&(cp_kind_prize(b->kind)||pose.p[2]>3*256)){
            point_t ground=project((rv3){pose.p[0],pose.p[1],0});
            if(ground.y+5>m->ymax)m->ymax=ground.y+5;
            if(ground.x-10<m->xmin)m->xmin=ground.x-10;
            if(ground.x+10>m->xmax)m->xmax=ground.x+10;
        }
        dirty_rect(m->xmin-2,m->ymin-2,m->xmax+2,m->ymax+2);
    }
    if(s_shadows)for(unsigned i=0;i<CP_MAX_COINS;++i){const cp_coin_t *b=&g->coins[i];const mesh_t *m=&s_mesh[i];
        if(!m->valid||!b->active||b->feeder||m->position[1]<=92*128||m->position[2]>=35*128||m->position[2]<0||(!cp_kind_prize(b->kind)&&m->position[2]<=3*128))continue;
        point_t point=project((rv3){m->position[0]*2,m->position[1]*2,0});
        s_shadows[s_shadow_count++]=(shadow_t){point.x,point.y,point.z,cp_kind_prize(b->kind)?9:7,0};
    }
    s_order_count=0;
    for(unsigned i=0;i<CP_MAX_COINS;++i){mesh_t *m=&s_mesh[i];if(!m->valid||!g->coins[i].active||g->coins[i].p.z<-20)continue;
        unsigned vertices=cp_kind_prize(m->kind)?8:16;uint16_t near=m->center.z;
#ifndef CP_RENDER_BAKE_COIN
        if(!cp_kind_prize(m->kind)&&!s_baking_coin){near=near>140?near-140:1;}else{
#endif
        for(unsigned k=1;k<vertices;++k){if(s_shapes[m->shape].p[k].z<near)near=s_shapes[m->shape].p[k].z;}
#ifndef CP_RENDER_BAKE_COIN
        }
#endif
        s_near[i]=near;
        unsigned at=s_order_count++;while(at&&s_near[s_order[at-1]]>near){s_order[at]=s_order[at-1];--at;}s_order[at]=(uint16_t)i;}
}
static int edge(point_t a,point_t b,int x,int y) {return (b.x-a.x)*(y-a.y)-(b.y-a.y)*(x-a.x);}
static __attribute__((noinline)) void polygon(const point_t *p,unsigned count,uint16_t fill,int texture)
{
    polygon_cache_t local,*cached=&local;bool fresh=true;
    bool uv=texture>=0&&texture<5;
    const uint8_t *map=uv?texture_map(texture):NULL;
    if(s_polygon_slot>=0&&(unsigned)s_polygon_slot<s_polygon_count&&s_polygons){cached=&s_polygons[s_polygon_slot++];fresh=cached->generation!=s_generation;}
    if(fresh){
        cached->generation=s_generation;cached->valid=false;
        point_t a=p[0],b=p[count/4],c=p[count/2];
        unsigned ua=count==4?0:31,va=count==4?0:15,ub=count==4?31:15,vb=count==4?0:31,uc=count==4?31:0,vc=count==4?31:15;
        int area=edge(a,b,c.x,c.y);if(!area)return;
        if(area<0){point_t t=b;b=c;c=t;unsigned q=ub;ub=uc;uc=q;q=vb;vb=vc;vc=q;area=-area;}
        int xmin=p[0].x,xmax=xmin,ymin=p[0].y,ymax=ymin;
        for(unsigned i=1;i<count;++i){if(p[i].x<xmin)xmin=p[i].x;if(p[i].x>xmax)xmax=p[i].x;if(p[i].y<ymin)ymin=p[i].y;if(p[i].y>ymax)ymax=p[i].y;}
        if(xmax<0||xmin>=s_width||ymax<0||ymin>=s_height)return;
        if(xmin<0)xmin=0;
        if(xmax>s_width-1)xmax=s_width-1;
        if(ymin<0)ymin=0;
        if(ymax>s_height-1)ymax=s_height-1;
        cached->xmin=xmin;cached->xmax=xmax;cached->ymin=ymin;cached->ymax=ymax;
        uint32_t inv=(1u<<30)/(unsigned)area;
        int ax=b.y-c.y,bx=c.y-a.y,cx=a.y-b.y,ay=c.x-b.x,by=a.x-c.x,cy=b.x-a.x;
        cached->dz=(int32_t)(((int64_t)(ax*a.z+bx*b.z+cx*c.z)*inv)>>22);
        cached->du=!uv?0:(int32_t)(((int64_t)(ax*(int)ua+bx*(int)ub+cx*(int)uc)*inv)>>22);
        cached->dv=!uv?0:(int32_t)(((int64_t)(ax*(int)va+bx*(int)vb+cx*(int)vc)*inv)>>22);
        cached->dzy=(int32_t)(((int64_t)(ay*a.z+by*b.z+cy*c.z)*inv)>>22);
        cached->duy=!uv?0:(int32_t)(((int64_t)(ay*(int)ua+by*(int)ub+cy*(int)uc)*inv)>>22);
        cached->dvy=!uv?0:(int32_t)(((int64_t)(ay*(int)va+by*(int)vb+cy*(int)vc)*inv)>>22);
        unsigned edge_count=0;
        for(unsigned i=0;i<count;++i){point_t l=p[i],r=p[(i+1)%count];if(l.y==r.y)continue;if(l.y>r.y){point_t q=l;l=r;r=q;}
            cached->edges[edge_count].x=l.x*65536;cached->edges[edge_count].dx=(r.x-l.x)*65536/(r.y-l.y);cached->edges[edge_count].y0=l.y;cached->edges[edge_count++].y1=r.y;}
        cached->edge_count=edge_count;
        int wa0=edge(b,c,xmin,ymin),wb0=edge(c,a,xmin,ymin),wc0=edge(a,b,xmin,ymin);
        cached->zr=(int32_t)((((int64_t)wa0*a.z+(int64_t)wb0*b.z+(int64_t)wc0*c.z)*inv)>>22);
        cached->ur=!uv?0:(int32_t)((((int64_t)wa0*ua+(int64_t)wb0*ub+(int64_t)wc0*uc)*inv)>>22);
        cached->vr=!uv?0:(int32_t)((((int64_t)wa0*va+(int64_t)wb0*vb+(int64_t)wc0*vc)*inv)>>22);
        cached->valid=true;
    }
    if(!cached->valid||cached->ymax<s_y0||cached->ymin>=s_y0+s_tile_h)return;
    int ymin=cached->ymin>s_y0?cached->ymin:s_y0,ymax=cached->ymax<s_y0+s_tile_h?cached->ymax:s_y0+s_tile_h-1;
    int xmin=cached->xmin>s_clip_x0?cached->xmin:s_clip_x0,xmax=cached->xmax<s_clip_x1?cached->xmax:s_clip_x1-1;
    if(xmin>xmax)return;
    int32_t dz=cached->dz,du=cached->du,dv=cached->dv;
    int32_t zr=cached->zr+cached->dzy*(ymin-cached->ymin)+dz*(xmin-cached->xmin),ur=cached->ur+cached->duy*(ymin-cached->ymin)+du*(xmin-cached->xmin),vr=cached->vr+cached->dvy*(ymin-cached->ymin)+dv*(xmin-cached->xmin);
    for(int y=ymin;y<=ymax;++y){
        int32_t lo=INT32_MAX,hi=INT32_MIN;
        for(unsigned i=0;i<cached->edge_count;++i){if(y<cached->edges[i].y0||y>cached->edges[i].y1)continue;int32_t x=cached->edges[i].x+cached->edges[i].dx*(y-cached->edges[i].y0);if(x<lo)lo=x;if(x>hi)hi=x;}
        if(lo!=INT32_MAX){
            int left=(lo+65535)>>16,right=hi>>16;if(left<xmin)left=xmin;if(right>xmax)right=xmax;
            int32_t z=zr+dz*(left-xmin),u=ur+du*(left-xmin),v=vr+dv*(left-xmin);
            unsigned off=(unsigned)(y-s_y0)*s_width+(unsigned)left;
            uint16_t line_fill=texture==5?(s_colors?s_colors->rows:cp_steel_rows)[(y+s_reflection_phase)&31]:fill;
            for(int x=left;x<=right;++x,++off){
                if((z>>8)<s_depth[off]&&z>0){uint16_t pixel=line_fill;
                    if(uv){int tx=u>>8,ty=v>>8;
                        if(tx<0)tx=0;else if(tx>31)tx=31;if(ty<0)ty=0;else if(ty>31)ty=31;
                        pixel=s_material[map[ty*32+tx]];
                    }

                    s_rgb[off]=pixel;s_depth[off]=(uint16_t)(z>>8);
                }
                z+=dz;u+=du;v+=dv;
            }
        }
        zr+=cached->dzy;ur+=cached->duy;vr+=cached->dvy;
    }
}
static CP_DRAW_HOT CP_NOIPA void quad_surface(rv3 a,rv3 b,rv3 c,rv3 d,uint16_t fill,int texture)
{
    point_t pa=project(a),pb=project(b),pc=project(c),pd=project(d);
    point_t points[4]={pa,pb,pc,pd};polygon(points,4,fill,texture);
}
static void quad(rv3 a,rv3 b,rv3 c,rv3 d,uint16_t fill){quad_surface(a,b,c,d,fill,-1);}
static CP_DRAW_HOT void slab(int32_t x0,int32_t x1,int32_t y0,int32_t y1,int32_t z,int32_t h,uint16_t fill)
{
    quad_surface((rv3){x0,y0,z},(rv3){x1,y0,z},(rv3){x1,y1,z},(rv3){x0,y1,z},fill,s_slab_texture);
    quad((rv3){x0,y1,z-h},(rv3){x1,y1,z-h},(rv3){x1,y1,z},(rv3){x0,y1,z},shade(fill,2662));
    quad((rv3){x1,y0,z-h},(rv3){x1,y1,z-h},(rv3){x1,y1,z},(rv3){x1,y0,z},shade(fill,1843));
}
static CP_DRAW_HOT void rect(int x,int y,int w,int h,uint16_t c)
{
    int x1=x+w,y1=y+h;
    if(x>=s_width||x1<=0||y>=s_y0+s_tile_h||y1<=s_y0)return;
    if(x<0)x=0;
    if(x1>s_width)x1=s_width;
    if(y<s_y0)y=s_y0;
    if(y1>s_y0+s_tile_h)y1=s_y0+s_tile_h;
    typedef uint32_t alias_u32 __attribute__((__may_alias__));uint32_t word=c|((uint32_t)c<<16);
    for(int yy=y;yy<y1;++yy){uint16_t *dst=s_rgb+(unsigned)(yy-s_y0)*s_width+(unsigned)x;int n=x1-x;
        if(((uintptr_t)dst&3)&&n>0){*dst++=c;--n;}
        while(n>=2){*(alias_u32 *)dst=word;dst+=2;n-=2;}if(n>0)*dst=c;
    }
}
static void feeder_disk(int32_t x,int32_t y,int32_t z,int32_t radius,uint16_t fill){
    point_t p[8];for(unsigned k=0;k<8;++k)p[k]=project((rv3){x+((rx[k]*radius)>>12),y,z+((ry[k]*radius)>>12)});
    polygon(p,8,fill,-1);
}
static void feeder_rod(void){
    if(s_y0>112||s_y0+s_tile_h<44)return;
    cp_rod_t rod=s_render_rod;int32_t ox=-(rod.z-CP_ROD_Z*256)*3/CP_ROD_LENGTH,oz=(rod.x-CP_ROD_X*256)*3/CP_ROD_LENGTH;
    rv3 a={CP_ROD_X*256+ox,31*256,CP_ROD_Z*256+oz},b={CP_ROD_X*256-ox,31*256,CP_ROD_Z*256-oz};
    rv3 c={rod.x-ox,31*256,rod.z-oz},d={rod.x+ox,31*256,rod.z+oz};
    s_material=s_colors?s_colors->steel:cp_palette_steel;quad_surface(a,b,c,d,color(241,169,65),5);
    quad((rv3){a.x,27*256,a.z},a,d,(rv3){d.x,27*256,d.z},color(111,83,49));
    feeder_disk(CP_ROD_X*256,33*256,CP_ROD_Z*256,4*256,color(236,220,159));
}
/* Native UTF-8 renderer bound explicitly to the checked 12/20 px CJK subset. */
static CP_DRAW_HOT void text_cn(int x,int y,const char *s,unsigned size,uint16_t ink){
    if(y+(int)size<=s_y0||y>=s_y0+s_tile_h)return;
    while(*s){uint32_t code=(uint8_t)*s++;
        if(code>=0xe0&&code<0xf0){code=(code&15)<<12;code|=((uint8_t)*s++&63)<<6;code|=(uint8_t)*s++&63;}
        else if(code>=0xc0&&code<0xe0){code=(code&31)<<6;code|=(uint8_t)*s++&63;}
        unsigned lo=0,hi=CP_GLYPH_COUNT;
        while(lo<hi){unsigned mid=(lo+hi)/2;if(cp_glyphs[mid].code<code)lo=mid+1;else hi=mid;}
        if(lo==CP_GLYPH_COUNT||cp_glyphs[lo].code!=code){rect(x,y,(int)size-2,(int)size-2,ink);x+=(int)size;continue;}
        const cp_glyph_t *glyph=&cp_glyphs[lo];
        for(unsigned row=0;row<size;++row){int yy=y+(int)row;if(yy<s_y0||yy>=s_y0+s_tile_h)continue;
            uint32_t bits=size==20?glyph->large[row]:glyph->small[row];
            while(bits){unsigned col=(unsigned)__builtin_ctz(bits);bits&=bits-1;int xx=x+(int)col;
                if(xx>=s_clip_x0&&xx<s_clip_x1)s_rgb[(yy-s_y0)*240+xx]=ink;}
        }
        x+=size==20?glyph->large_advance:glyph->small_advance;if(x>=240)break;
    }
}
/* Camera and cabinet geometry are fixed. Bake their color and exact depth
 * once into flash, then copy a tile instead of rasterizing the cabinet at 30 Hz. */
void cp_render_background_tile(int y0,uint16_t *rgb,uint16_t *depth)
{
    s_native_x0=0;s_native_x1=240;scene_context(false);s_polygon_slot=-1;s_rgb=rgb;s_depth=depth;s_y0=y0;
    for(unsigned i=0;i<240*CP_TILE_H;++i) {rgb[i]=color(13,22,29);depth[i]=65535;}
    /* Upright backboard and a real inclined guide joining the raised deck. */
    quad((rv3){18*256,22*256,43*256},(rv3){190*256,22*256,43*256},
         (rv3){190*256,22*256,154*256},(rv3){18*256,22*256,154*256},color(30,49,62));
    quad((rv3){18*256,26*256,42*256},(rv3){190*256,26*256,42*256},
         (rv3){190*256,72*256,27*256},(rv3){18*256,72*256,27*256},color(91,117,128));
    for(int x=28;x<190;x+=12)quad((rv3){x*256,26*256,42*256+16},(rv3){(x+1)*256,26*256,42*256+16},
         (rv3){(x+1)*256,72*256,27*256+16},(rv3){x*256,72*256,27*256+16},color(68,92,103));
    for(unsigned k=0;k<CP_FEEDER_PEGS;++k){const cp_peg_t *peg=&cp_feeder_pegs[k];
        feeder_disk(peg->x*256,31*256,peg->z*256,640,color(147,170,179));
        feeder_disk(peg->x*256-80,32*256,peg->z*256+80,220,color(227,234,222));}
    quad((rv3){96*256,30*256,154*256},(rv3){112*256,30*256,154*256},
         (rv3){112*256,30*256,158*256},(rv3){96*256,30*256,158*256},color(220,171,65));
    /* Deep side gutters, lower payout lip, the elevated translating shelf. */
    slab(0,208*256,0,(int32_t)((CP_FRONT_EDGE+17)*256),-12*256,8*256,color(30,43,50));
    slab(12*256,196*256,0,CP_SIDE_EDGE*256,0,5*256,color(69,91,94));
    slab(18*256,190*256,CP_SIDE_EDGE*256,(int32_t)(CP_FRONT_EDGE*256),0,5*256,color(69,91,94));
    slab(0,12*256,0,CP_SIDE_EDGE*256,23*256,24*256,color(48,65,75));
    slab(196*256,208*256,0,CP_SIDE_EDGE*256,23*256,24*256,color(48,65,75));
    slab(12*256,196*256,14*256,18*256,34*256,16*256,color(104,128,141));
    slab(18*256,190*256,(int32_t)(CP_FRONT_EDGE*256),(int32_t)((CP_FRONT_EDGE+2)*256),0,8*256,color(223,165,44));
#if defined(CP_RENDER_HOST) && !defined(CP_RENDER_BAKE)
    for(unsigned row=0;row<CP_TILE_H;++row)for(unsigned x=0;x<240;++x){
        unsigned at=row*240+x;rgb[at]=cabinet_pixel(x,(unsigned)y0+row,rgb[at]);}
#endif
}
#ifdef CP_RENDER_HOST
static uint32_t s_background[16384];
static bool s_background_loaded;
static const uint8_t *background(void){
    if(!s_background_loaded){FILE *f=fopen("main/pusher_background.bin","rb");
        if(!f){return NULL;}size_t size=fread(s_background,1,sizeof(s_background),f);fclose(f);
        const cp_bg_header_t *h=(const cp_bg_header_t *)s_background;
        if(size<sizeof(*h)||h->magic!=CP_BG_MAGIC||h->width!=240||h->height!=320||sizeof(*h)+(size_t)h->offset[320]*sizeof(cp_bg_run_t)!=size)return NULL;
        s_background_loaded=true;}
    return (const uint8_t *)s_background;
}
#else
extern const uint8_t background_start[] asm("_binary_pusher_background_bin_start");
static const uint8_t *background(void){return background_start;}
#endif
static CP_DRAW_HOT bool base_region(int y0,uint16_t *rgb,uint16_t *depth,int x0,int x1,int ybegin,int yend){
    const uint8_t *data=background();if(!data)return false;
    const cp_bg_header_t *h=(const cp_bg_header_t *)data;const cp_bg_run_t *runs=(const cp_bg_run_t *)(data+sizeof(*h));
    typedef uint32_t alias_u32 __attribute__((__may_alias__));
#if CP_PHOTO_RGB
    /* Sequential native copies avoid palette expansion and instruction/data cache churn. */
    for(int y=ybegin;y<yend;++y)memcpy(rgb+(y-y0)*240+x0,cp_cabinet_pixels+y*240+x0,(unsigned)(x1-x0)*2);
#else
    /* Raw palette indices halve Flash bandwidth without changing any pixel.
     * Expand only the dirty columns, using the shared-workspace palette. */
    typedef uint16_t alias_u16 __attribute__((__may_alias__));
    const uint16_t *palette=s_photo_palette?s_photo_palette:cp_photo_palette;
    for(int y=ybegin;y<yend;++y){
        uint16_t *pixels=rgb+(y-y0)*240+x0;const uint8_t *indices=cp_cabinet_indices+y*240+x0;
        if((uintptr_t)pixels&3){for(int x=x0;x<x1;++x)*pixels++=palette[*indices++];continue;}
        unsigned n=(unsigned)(x1-x0)/2;alias_u32 *dst=(alias_u32 *)pixels;
        if(((uintptr_t)indices&3)&&n){unsigned k=*(const volatile alias_u16 *)indices;*dst++=palette[k&255]|((uint32_t)palette[k>>8]<<16);indices+=2;--n;}
        const volatile alias_u32 *src=(const volatile alias_u32 *)indices;
        while(n>=4){unsigned a=src[0],b=src[1];
            dst[0]=palette[a&255]|((uint32_t)palette[(a>>8)&255]<<16);dst[1]=palette[(a>>16)&255]|((uint32_t)palette[a>>24]<<16);
            dst[2]=palette[b&255]|((uint32_t)palette[(b>>8)&255]<<16);dst[3]=palette[(b>>16)&255]|((uint32_t)palette[b>>24]<<16);dst+=4;src+=2;n-=4;
        }
        const volatile alias_u16 *tail=(const volatile alias_u16 *)src;
        while(n--){unsigned k=*tail++;*dst++=palette[k&255]|((uint32_t)palette[k>>8]<<16);}
    }
#endif
    for(int y=ybegin;y<yend;++y){unsigned x=0,base=(unsigned)(y-y0)*240;
        for(unsigned i=h->offset[y];i<h->offset[y+1];++i){const cp_bg_run_t *r=&runs[i];unsigned end=x+r->length;
            unsigned at=x>(unsigned)x0?x:(unsigned)x0,stop=end<(unsigned)x1?end:(unsigned)x1;
            if(at<stop){unsigned n=stop-at;int32_t step=r->step;uint32_t z=r->start+step*(int32_t)(at-x);
                if(cabinet_detail(r->color))for(unsigned k=at;k<stop;++k)rgb[base+k]=r->color;
                if(((uintptr_t)(depth+base+at)&3)&&n){depth[base+at]=(uint16_t)(z>>8);++at;--n;z+=step;}
                while(n>=2){*(alias_u32 *)(depth+base+at)=(z>>8)|(((z+step)>>8)<<16);at+=2;n-=2;z+=step*2;}
                if(n)depth[base+at]=(uint16_t)(z>>8);
            }
            x=end;if(x>=(unsigned)x1)break;
        }
    }
    return true;
}
bool cp_render_base_tile(int y0,uint16_t *rgb,uint16_t *depth){return base_region(y0,rgb,depth,0,240,y0,y0+CP_TILE_H);}
static CP_DRAW_HOT void contact_shadows(void){
    static const int8_t extent[2][7]={{0,5,6,7,6,5,0},{0,6,8,9,8,6,0}};
    for(unsigned i=0;i<s_shadow_count;++i){const shadow_t *p=&s_shadows[i];
        if(p->y+3<s_y0||p->y-3>=s_y0+s_tile_h)continue;
        for(int dy=-3;dy<=3;++dy){int y=p->y+dy;if(y<s_y0||y>=s_y0+s_tile_h)continue;
            int half=extent[p->radius==9][dy+3],left=p->x-half,right=p->x+half;
            if(left<s_clip_x0)left=s_clip_x0;
            if(right>=s_clip_x1)right=s_clip_x1-1;
            unsigned at=(unsigned)(y-s_y0)*240+(unsigned)left;
            for(int x=left;x<=right;++x,++at){int dz=(int)s_depth[at]-(int)p->depth;
                if(dz>-20&&dz<20){uint16_t c=s_rgb[at];s_rgb[at]=(uint16_t)(((c&0xf7de)>>1)+((c&0xe79c)>>2));}
            }
        }
    }
}
static CP_DRAW_HOT void mesh_raster(unsigned i,const cp_coin_t *b){
    mesh_t *m=&s_mesh[i];mesh_shape_t *shape=&s_shapes[m->shape];
        #if defined(ESP_PLATFORM) || defined(CP_RENDER_COMPACT)
        unsigned n=4; /* Only prize boxes use mesh geometry; coins use the exact atlas. */
#else
        unsigned n=cp_kind_prize(b->kind)?4:8;
#endif
        s_polygon_slot=6+(int)m->shape;
        const uint16_t *palette=(b->trait&CP_TRAIT_CROWN)||!cp_kind_prize(b->kind)?(s_colors?s_colors->gold:cp_lit_gold):b->kind==CP_PRIZE_RED?(s_colors?s_colors->red:cp_lit_red):b->kind==CP_PRIZE_BLUE?(s_colors?s_colors->blue:cp_lit_blue):(s_colors?s_colors->green:cp_lit_green);
        s_material=palette+shape->light*16;
        polygon(shape->p+(shape->top?0:n),n,shape->top?shape->cap:shape->bottom,n==8?1:(2+b->kind-CP_PRIZE_RED));
        s_polygon_slot=-1;
        for(unsigned k=0;k<n;++k){if(!(shape->visible&(1u<<k)))continue;unsigned j=(k+1)%n;
            if(n==4)s_material=(b->trait&CP_TRAIT_CROWN)?(s_colors?s_colors->gold:cp_lit_gold)+48:s_colors?s_colors->sides[b->kind-CP_PRIZE_RED]:b->kind==CP_PRIZE_RED?cp_palette_red_side:b->kind==CP_PRIZE_BLUE?cp_palette_blue_side:cp_palette_green_side;
            point_t side[4]={shape->p[k],shape->p[k+n],shape->p[j+n],shape->p[j]};polygon(side,4,shape->side[k],n==4?(2+b->kind-CP_PRIZE_RED):-1);
        }
}
#ifndef CP_RENDER_BAKE_COIN
#ifdef CP_RENDER_HOST
static uint16_t *s_atlas_data;
static const uint16_t *atlas_data(void){
    if(!s_atlas_data){FILE *f=fopen("main/pusher_coin_impostors.bin","rb");if(!f)return NULL;
        if(fseek(f,0,SEEK_END)){fclose(f);return NULL;}long size=ftell(f);rewind(f);
        if(size!=CP_ATLAS_BYTES){fclose(f);return NULL;}
        s_atlas_data=malloc((size_t)size);if(!s_atlas_data){fclose(f);return NULL;}
        if(fread(s_atlas_data,1,(size_t)size,f)!=(size_t)size){free(s_atlas_data);s_atlas_data=NULL;}fclose(f);}
    return s_atlas_data;
}
#else
extern const uint16_t atlas_start[] asm("_binary_pusher_coin_impostors_bin_start");
extern const uint16_t atlas_end[] asm("_binary_pusher_coin_impostors_bin_end");
static const uint16_t *atlas_data(void){return atlas_start;}
#endif
bool cp_render_assets_ready(void){
#ifdef CP_RENDER_HOST
    return background()!=NULL&&atlas_data()!=NULL;
#else
    return (const uint8_t *)atlas_end-(const uint8_t *)atlas_start==CP_ATLAS_BYTES;
#endif
}
static CP_DRAW_HOT void coin_impostor(const mesh_t *mesh){
    const uint16_t *data=atlas_data();if(!data)return;
    const uint16_t *spans=data+cp_atlas_offsets[mesh->atlas],*pixels=spans+20;
    const uint16_t *gold_palette=s_colors?s_colors->atlas:cp_atlas_palette;
    int xbase=mesh->center.x-10,ybase=mesh->center.y-10,center=mesh->center.z;
    for(unsigned row=0;row<20;++row){unsigned span=spans[row],left=span&31,n=span>>5;
        int y=ybase+(int)row,x=xbase+(int)left;
        if(y>=s_y0&&y<s_y0+s_tile_h&&n){int begin=x>s_clip_x0?x:s_clip_x0,end=x+(int)n;if(end>s_clip_x1)end=s_clip_x1;
            if(begin>=end){pixels+=n;continue;}
            unsigned off=(unsigned)(y-s_y0)*240+(unsigned)begin;
            const uint16_t *source=pixels+begin-x;
            for(int xx=begin;xx<end;++xx,++source,++off){unsigned packed=*source;
                if(!(packed&0x8000))continue;
                int z=center+(((int)((packed>>5)&255)-128)*2);
                if(z>0&&z<s_depth[off]){uint16_t tint=gold_palette[packed&31];
                    if(mesh->kind==CP_CHIP){unsigned light=((tint>>11)*2+((tint>>5)&63)+(tint&31))/4;
                        tint=((xx-xbase<3||xx-xbase>16)&&((row/3)&1))?color(218,240,255):color(24+light,74+light*2,130+light*3);}
                    s_rgb[off]=tint;s_depth[off]=(uint16_t)z;}
            }
        }
        pixels+=n;
    }
}
#endif
#ifdef CP_RENDER_HOST
void cp_render_bake_coin(cp_q4 pose,uint16_t *rgb,int16_t *relative_depth){
    cp_coin_t body={.kind=CP_COIN_GOLD,.q=pose};cp_body_refresh(&body);
    const float ay=2949.0f/4096,az=-3359.0f/4096,scale=ay*ay+az*az;
    cp_v3 direction=cp_v(0,-az,ay),local_direction;
    local_direction=cp_v(cp_dot(direction,cp_body_axis(&body,0)),cp_dot(direction,cp_body_axis(&body,1)),cp_dot(direction,cp_body_axis(&body,2)));
    cp_v3 planes[10]={{0,0,1},{0,0,-1}};
    for(unsigned k=0;k<8;++k){float angle=(k+.5f)*3.14159265358979323846f/4;planes[k+2]=cp_v(cosf(angle),sinf(angle),0);}
    cp_v3 light=cp_v(-1303.0f/4096,-1114.0f/4096,3719.0f/4096);
    unsigned level=(unsigned)(fabsf(cp_dot(light,cp_body_axis(&body,2)))*8);if(level>7)level=7;
    for(int y=0;y<20;++y)for(int x=0;x<20;++x){unsigned at=(unsigned)y*20+(unsigned)x;rgb[at]=0;relative_depth[at]=INT16_MAX;
        cp_v3 origin=cp_v((float)x-10,((float)y-10)*ay/scale,((float)y-10)*az/scale);
        cp_v3 local=cp_v(cp_dot(origin,cp_body_axis(&body,0)),cp_dot(origin,cp_body_axis(&body,1)),cp_dot(origin,cp_body_axis(&body,2)));
        float low=-10000,high=10000;unsigned hit=0;bool inside=true;
        for(unsigned k=0;k<10;++k){float limit=k<2?1.3f:7.39103626f;
            float p=cp_dot(local,planes[k]),d=cp_dot(local_direction,planes[k]);
            if(fabsf(d)<.00001f){if(p>limit){inside=false;break;}continue;}
            float t=(limit-p)/d;
            if(d>0){if(t<high){high=t;hit=k;}}else if(t>low)low=t;
            if(low>high){inside=false;break;}
        }
        if(!inside)continue;
        cp_v3 point=cp_add(origin,cp_mul(direction,high));
        relative_depth[at]=(int16_t)lroundf(-12*point.y-(2703.0f/256)*point.z);
        if(hit<2){cp_v3 p=cp_add(local,cp_mul(local_direction,high));
            int u=(int)lroundf(15.5f+p.x*31/16),v=(int)lroundf(15.5f+p.y*31/16);
            if(u<0)u=0;if(u>31)u=31;if(v<0)v=0;if(v>31)v=31;
            rgb[at]=cp_lit_gold[level*16+cp_metal_gold[v*32+u]];
        }else{cp_v3 normal=cp_add(cp_mul(cp_body_axis(&body,0),planes[hit].x),cp_mul(cp_body_axis(&body,1),planes[hit].y));
            float intensity=cp_dot(normal,light);if(intensity<0)intensity=0;
            rgb[at]=shade(color(255,210,75),1556+(int32_t)(1843*intensity));}
    }
}
#endif
static __attribute__((noinline)) void arcade_scene(const cp_game_t *g){
    if(!g->tactics.fx_ticks&&(s_y0+s_tile_h<170||s_y0>280))return;
    s_polygon_slot=(int)s_shape_count+10;
    if(g->arcade.tower_ticks){int wall=cp_tower_pusher_mm(g)*256;
        bool right=g->arcade.tower_side;int x0=right?196*256:-10*256,x1=right?218*256:12*256;
        slab(x0,x1,84*256,126*256,0,3*256,color(118,132,140));
        quad((rv3){wall,85*256,0},(rv3){wall,125*256,0},(rv3){wall,125*256,22*256},(rv3){wall,85*256,22*256},color(209,169,67));
        int front=wall+(right?-17:17)*256;
        quad((rv3){front,87*256,21*256},(rv3){front,123*256,21*256},(rv3){front,123*256,22*256},(rv3){front,87*256,22*256},color(169,204,211));
        for(unsigned edge=0;edge<2;++edge){int y=(87+(int)edge*36)*256;
            quad((rv3){front,y,0},(rv3){front,y+256,0},(rv3){front,y+256,22*256},(rv3){front,y,22*256},color(128,166,179));}
    }
    for(unsigned lane=0;lane<3;++lane){int x=(53+(int)lane*51)*256;
        quad((rv3){x-10*256,72*256,19*256},(rv3){x+10*256,72*256,19*256},(rv3){x+10*256,72*256,22*256},(rv3){x-10*256,72*256,22*256},color(73,61,38));
        bool active=cp_rogue_lane(g,lane);
        for(unsigned k=0;k<5;++k){int left=x-8*256+(int)k*3*256;unsigned phase=(g->steps/3+k+lane*3)%12;
            uint16_t lamp=active?(phase<5?color(255,208-(int)phase*13,70):color(128,108,47)):color(27,58,62);
            quad((rv3){left,72*256-30,19*256+90},(rv3){left+2*256,72*256-30,19*256+90},(rv3){left+2*256,72*256-30,22*256-90},(rv3){left,72*256-30,22*256-90},lamp);}
    }
    if(g->rogue.active&&(g->rogue.storm_ticks||g->rogue.float_ticks||g->rogue.ripple_ticks)){
        int radius=18+(g->steps%20)*2;uint16_t glow=g->rogue.storm_ticks?color(89,226,255):color(169,138,255);
        for(unsigned k=0;k<8;++k){unsigned j=(k+1)%8;
            rv3 a={104*256+rx[k]*radius/16,122*256+ry[k]*radius/32,544};
            rv3 b={104*256+rx[j]*radius/16,122*256+ry[j]*radius/32,544};
            rv3 c={104*256+rx[j]*(radius+2)/16,122*256+ry[j]*(radius+2)/32,544};
            rv3 d={104*256+rx[k]*(radius+2)/16,122*256+ry[k]*(radius+2)/32,544};quad(a,b,c,d,glow);
        }
    }
    if(g->tactics.fx_ticks){
        int radius=8+(24-g->tactics.fx_ticks)*2;
        for(unsigned k=0;k<8;++k){unsigned j=(k+1)%8;
            int x=g->tactics.fx_x*256,y=g->tactics.fx_y*256;
            rv3 a={x+rx[k]*radius/16,y+ry[k]*radius/16,560},b={x+rx[j]*radius/16,y+ry[j]*radius/16,560};
            rv3 c={x+rx[j]*(radius+1)/16,y+ry[j]*(radius+1)/16,560},d={x+rx[k]*(radius+1)/16,y+ry[k]*(radius+1)/16,560};
            quad(a,b,c,d,g->tactics.fx_kind==CP_SP_SEED?color(120,255,126):color(110,224,255));
        }
    }
    if(g->arcade.hole_ticks){
        for(unsigned k=0;k<8;++k){unsigned j=(k+1)%8;
            rv3 a={104*256,122*256,512},b={104*256+rx[k]*18/16,122*256+ry[k]*18/16,512},c={104*256+rx[j]*18/16,122*256+ry[j]*18/16,512};
            quad(a,b,c,a,color(5,3,16));
            rv3 d={104*256+rx[k]*23/16,122*256+ry[k]*23/16,544},e={104*256+rx[j]*23/16,122*256+ry[j]*23/16,544};
            quad(b,d,e,c,((k+g->steps/3)&3)?color(82,41,154):color(190,112,255));
        }
        for(unsigned k=0;k<10;++k){unsigned turn=(g->steps/2+k)%8,r=9+(k*3+g->steps)%23;
            point_t p=project((rv3){104*256+rx[turn]*(int)r/16,122*256+ry[turn]*(int)r/16,2*256});
            /* Small particles stay on the floor, underneath the coin meshes. */
            if(p.y>=s_y0&&p.y<s_y0+s_tile_h&&p.x>=s_clip_x0&&p.x<s_clip_x1)s_rgb[(p.y-s_y0)*240+p.x]=color(192,154,255);
        }
    }
}
static const char *event_name(unsigned event){
 switch(event){case CP_EVENT_RUSH:return "超载推进";case CP_EVENT_RAIN:return "黄金瀑布";case CP_EVENT_SAVE:return "幸运护航";
 case CP_EVENT_JACKPOT:return "皇家宝库";case CP_EVENT_BLACKHOLE:return "虚空虹吸";case CP_EVENT_DOUBLE:return "星潮增殖";
 case CP_EVENT_TOWERS:return "双塔降临";case CP_EVENT_SHAKE:return "地脉震荡";default:return "极光狂热";}
}
static const char *event_detail(unsigned event){
 static const char *const detail[]={"","推板进入超载节奏","奖励金币依次入场","限时守护两侧边槽","金币瀑布与护航加速","中央引力回收金币","追加一组实体金币","侧向送入实体金币塔","冲击撼动盘面币堆","限时提升入袋收益"};
 return detail[event<=CP_EVENT_MAX?event:0];
}
/* Integer chase lights share the moving shelf/coin dirty regions. No new image buffer. */
static uint16_t jewel_light(unsigned phase,unsigned strength){
 unsigned k=phase%96;unsigned glow=k<48?k:96-k;
 return color(24+strength*glow/48,85+glow*3,94+strength/2);
}
static void arcade_marquee(const cp_game_t *g){
 unsigned phase=g->steps/3;bool celebration=g->arcade.spin_ticks||g->arcade.reveal_ticks||g->event_ticks;
 for(unsigned k=0;k<20;++k){unsigned t=(k+phase)%20;uint16_t c=celebration?(t<5?color(255,205-(int)t*17,72):color(52,43,24)):jewel_light(phase+k*5,108);
  rect(22+(int)k*10,193,7,2,c);rect(22+(int)k*10,196,7,1,color(48,56,49));}
 for(unsigned k=0;k<8;++k){uint16_t c=(k+phase)%8<3?color(255,203,88):color(54,92,92);int y=209+(int)k*8;
  rect(15,y,2,5,c);rect(222,265-(int)k*8,2,5,c);}
}
static const cp_records_t *s_records;
void cp_render_records(const cp_records_t *b){s_records=b;}
static __attribute__((noinline)) void rogue_dirty(const cp_game_t *g){
 static uint32_t previous;static unsigned old_view;const cp_rogue_t *r=&g->rogue;
 uint32_t h=r->revision^r->discovered^r->synergies;
 const unsigned v[]={g->bank==0,g->tactics.stage,g->tactics.queue_count,cp_tactics_auto_kind(g),cp_tactics_earned(g),cp_tactics_seconds(g),g->tactics.fx_ticks/6,g->tactics.blocked_ticks>0,g->tactics.crown_collected,g->tactics.selected,r->view,r->cursor,r->inspect,r->rank,r->xp,r->level,r->grace/30,(r->toast_ticks+29)/30,r->practice,r->store_failed,r->rerolls,r->synergies?g->steps/90:0,cp_rogue_lane(g,0)|(cp_rogue_lane(g,1)<<1)|(cp_rogue_lane(g,2)<<2)};
 for(unsigned k=0;k<sizeof(v)/sizeof(v[0]);++k)h=(h^v[k])*16777619u;
 if(r->active&&(h!=previous||r->view!=old_view)){if(r->view!=CP_VIEW_PLAY||old_view!=CP_VIEW_PLAY)dirty_y(0,319);else{dirty_y(0,53);dirty_y(274,319);}dirty_rect(20,182,220,199);}
 if(r->active&&(r->storm_ticks||r->float_ticks||r->ripple_ticks))dirty_rect(58,218,182,274);
 static bool old_field;bool field=r->active&&(r->storm_ticks||r->float_ticks||r->ripple_ticks);
 static bool old_tactics_fx;
 if(g->tactics.fx_ticks||old_tactics_fx)dirty_rect(0,180,239,274);
 old_tactics_fx=g->tactics.fx_ticks>0;
 if(old_field&&!field){dirty_rect(58,218,182,274);}
 old_field=field;previous=h;old_view=r->view;
}
static __attribute__((noinline)) void rogue_overlay(const cp_game_t *g){
 const cp_rogue_t *r=&g->rogue;if(!r->active)return;
 if(r->view==CP_VIEW_PLAY&&s_y0>=53&&s_y0+s_tile_h<=274)return;
 uint16_t bg=color(10,17,25),panel=color(23,34,46),ink=color(227,235,240),muted=color(144,164,179),gold=color(255,210,104),cyan=color(99,228,218);
 char t[112];unsigned owned=0;for(unsigned k=0;k<CP_RELIC_COUNT;++k)owned+=r->relic[k]>0;
 if(r->view==CP_VIEW_PLAY){
  if(s_y0<53){
  rect(0,0,240,53,bg);text_cn(18,4,"命运推币机",20,gold);
  snprintf(t,sizeof(t),"%u级",r->level);text_cn(157,9,t,12,cyan);
  snprintf(t,sizeof(t),"投币 %lu",(unsigned long)g->bank);text_cn(18,29,t,12,ink);
  snprintf(t,sizeof(t),"筹码 %lu",(unsigned long)g->score);text_cn(108,29,t,12,gold);
  snprintf(t,sizeof(t),"%u/5幕 %u/%u %u秒",g->tactics.stage+1,cp_tactics_earned(g),cp_tactics_goal(g),cp_tactics_seconds(g));text_cn(18,42,t,12,muted);
  }
  if(s_y0+s_tile_h<=274)return;
  rect(0,274,240,46,bg);
  if(!g->bank&&!g->tactics.queue_count){snprintf(t,sizeof(t),"等待入袋补给 %u秒",(CP_RUN_GRACE-r->grace+29)/30);text_cn(18,276,t,12,gold);}
  else if(g->tactics.blocked_ticks){text_cn(18,276,"入口暂忙 队列与次数保留",12,gold);}
  else if(g->tactics.fx_ticks){snprintf(t,sizeof(t),"触发 %s",cp_special_info[g->tactics.fx_kind-1].name);text_cn(18,276,t,12,cyan);}
  else if(r->toast_ticks){snprintf(t,sizeof(t),"获得 %s %u级",cp_relic_info[r->inspect].name,r->relic[r->inspect]);text_cn(18,276,t,12,cyan);}
  else if(r->synergies){unsigned k=(g->steps/90)%8;while(!(r->synergies&(1u<<k)))k=(k+1)%8;snprintf(t,sizeof(t),"共鸣 %s",cp_synergy_names[k]);text_cn(18,276,t,12,cyan);}
  else if(g->event_ticks){snprintf(t,sizeof(t),"事件 %s  免费币 %u",event_name(g->event_kind),g->bonus_queue);text_cn(18,276,t,12,gold);}
  else {snprintf(t,sizeof(t),"升级 %u/%u  免费币 %u",r->xp,r->goal,g->bonus_queue);text_cn(18,276,t,12,muted);}
  unsigned next=cp_tactics_auto_kind(g);
  if(next){snprintf(t,sizeof(t),"下一枚 %s 队列%u",cp_special_info[next-1].name,g->tactics.queue_count);text_cn(18,291,t,12,ink);}
  else text_cn(18,291,g->autoplay?"自动投币 上键停止":"确定投币 上键自动",12,ink);
  if(g->tactics.stage==CP_ACT_COUNT-1)text_cn(18,306,g->tactics.crown_collected?"皇冠已入袋 冲刺本幕目标":"终幕挑战 推落塔顶皇冠",12,muted);
  else{snprintf(t,sizeof(t),"本幕入袋 %lu/%u 下键队列",(unsigned long)(g->collected-g->tactics.stage_collected),cp_tactics_quota(g));text_cn(18,306,t,12,muted);}
  return;
 }
 rect(0,0,240,320,bg);rect(18,45,204,1,color(102,82,45));
 const char *title=r->view==CP_VIEW_TRAY?"自动投币队列":r->view==CP_VIEW_SUPPLY?"幕间补给":r->view==CP_VIEW_DRAFT?"命运三选一":r->view==CP_VIEW_PAUSE?"机台菜单":r->view==CP_VIEW_RANK?"本机排行榜":r->view==CP_VIEW_CODEX?"强化图鉴":r->view==CP_VIEW_SHOP?"筹码兑换":"本局结算";
 text_cn(20,12,title,20,gold);
 if(r->view==CP_VIEW_TRAY){
  text_cn(18,53,"获得即入队 投币自动使用",12,muted);
  unsigned page=g->tactics.selected/4;
  for(unsigned k=0;k<4;++k){unsigned id=page*4+k;int y=73+(int)k*36;bool selected=id==g->tactics.selected;
   rect(18,y,204,32,selected?color(46,59,63):panel);rect(18,y,3,32,selected?gold:panel);
   text_cn(27,y+5,cp_special_info[id].name,20,selected?gold:ink);
   snprintf(t,sizeof(t),"%u枚",g->tactics.stock[id]);text_cn(181,y+11,t,12,cyan);
  }
  unsigned id=g->tactics.selected;text_cn(20,228,cp_special_info[id].line1,12,ink);text_cn(20,247,cp_special_info[id].line2,12,cyan);
  snprintf(t,sizeof(t),"队列%u枚  当前浏览%u/8",g->tactics.queue_count,id+1);text_cn(20,270,t,12,muted);
  text_cn(20,290,"上下查看 确定返回机台",12,ink);text_cn(24,306,"无需选币 按获得顺序投放",12,muted);
 }else if(r->view==CP_VIEW_SUPPLY){
  snprintf(t,sizeof(t),"第%u幕 选择下一程合约",g->tactics.stage+1);text_cn(18,53,t,12,cyan);
  static const char *const names[]={"稳健补给","构筑补给","高压合约"};
  static const char *const one[]={"补给十二次投币与护航币","补给七次投币与两枚组合币","补给十八次投币与双塔"};
  static const char *const two[]={"维持标准目标 稳住节奏","优先补充当前强化关联币","本幕筹码目标提高四分之一"};
  for(unsigned k=0;k<3;++k){int y=75+(int)k*63;rect(18,y,204,58,k==r->cursor?color(46,59,63):panel);
   text_cn(27,y+5,names[k],20,k==r->cursor?gold:ink);text_cn(27,y+29,one[k],12,ink);text_cn(27,y+44,two[k],12,muted);}
  text_cn(20,274,"每幕重新计分 消费不倒扣进度",12,gold);
  text_cn(20,293,"上下选择 确定签订合约",12,ink);
 }else if(r->view==CP_VIEW_DRAFT){
  snprintf(t,sizeof(t),"等级 %u  投币 %lu  筹码 %lu",r->level,(unsigned long)g->bank,(unsigned long)g->score);text_cn(18,53,t,12,muted);
  for(unsigned k=0;k<3;++k){unsigned id=r->offer[k];int y=74+(int)k*64;
   rect(15,y,210,59,k==r->cursor?color(46,59,63):panel);rect(15,y,3,59,k==r->cursor?gold:panel);
   text_cn(24,y+5,cp_relic_info[id].name,20,k==r->cursor?gold:ink);
   snprintf(t,sizeof(t),"%u/3",r->relic[id]+1);text_cn(186,y+10,t,12,cyan);
   cp_rogue_effect(id,r->relic[id]+1,t,sizeof(t));text_cn(24,y+29,t,12,ink);text_cn(24,y+44,cp_relic_info[id].line2,12,muted);
  }
  snprintf(t,sizeof(t),"附赠 %s 一枚",cp_special_info[cp_tactics_relic_coin(r->offer[r->cursor])-1].name);text_cn(24,275,t,12,cyan);
  snprintf(t,sizeof(t),"长按确定重抽 剩余 %u",r->rerolls);text_cn(24,293,t,12,muted);
 }else if(r->view==CP_VIEW_PAUSE){
  snprintf(t,sizeof(t),"投币 %lu  筹码 %lu",(unsigned long)g->bank,(unsigned long)g->score);text_cn(20,57,t,12,cyan);
  static const char *const names[]={"继续推币","本局强化与图鉴","本机排行榜","用筹码购买投币",NULL,"结算离开本局"};
  for(unsigned k=0;k<6;++k){int y=77+(int)k*30;rect(18,y,204,26,k==r->cursor?color(46,59,63):panel);text_cn(30,y+6,k==4?(r->muted?"音效：静音":"音效：开启"):names[k],12,k==r->cursor?gold:ink);}
  text_cn(24,273,"菜单中暂停物理与倒计时",12,muted);text_cn(24,294,"上下选择 确定进入",12,ink);
 }else if(r->view==CP_VIEW_RANK){
  text_cn(22,55,"名次",12,muted);text_cn(58,55,"筹码",12,muted);text_cn(137,55,"等级",12,muted);text_cn(180,55,"投币",12,muted);
  unsigned page=r->rank/5;
  for(unsigned j=0;j<5;++j){unsigned k=page*5+j;int y=76+(int)j*32;
   rect(18,y,204,28,k==r->rank?color(46,59,63):panel);uint16_t row_ink=k==r->rank?gold:ink;
   snprintf(t,sizeof(t),"%u",k+1);text_cn(26,y+8,t,12,row_ink);
   if(s_records&&s_records->rows[k].sequence){const cp_record_t *row=&s_records->rows[k];
    snprintf(t,sizeof(t),"%lu",(unsigned long)row->chips);text_cn(58,y+8,t,12,row_ink);
    snprintf(t,sizeof(t),"%u",row->level);text_cn(142,y+8,t,12,row_ink);
    snprintf(t,sizeof(t),"%u",row->paid);text_cn(180,y+8,t,12,row_ink);
   }else text_cn(58,y+8,"暂无成绩",12,row_ink);
  }
  if(s_records&&s_records->rows[r->rank].sequence){snprintf(t,sizeof(t),"种子 %08lX",(unsigned long)s_records->rows[r->rank].seed);text_cn(24,247,t,12,cyan);}
  text_cn(24,275,"确定查看当局强化组合",12,ink);text_cn(24,294,"上下翻页 长按确定返回",12,muted);
 }else if(r->view==CP_VIEW_CODEX){
  unsigned id=r->inspect,level=r->relic[id];uint32_t synergy=r->synergies;bool history=r->back==CP_VIEW_RANK;
  if(history){uint8_t levels[CP_RELIC_COUNT]={0};if(s_records)for(unsigned k=0;k<CP_RELIC_COUNT;++k)levels[k]=(uint8_t)cp_record_level(&s_records->rows[r->rank],k);level=levels[id];synergy=cp_relic_synergies(levels);}
  snprintf(t,sizeof(t),"%02u/28   %s",id+1,history?"历史组合":"本局组合");text_cn(20,56,t,12,cyan);
  rect(18,80,204,111,panel);text_cn(27,91,cp_relic_info[id].name,20,gold);
  snprintf(t,sizeof(t),"等级 %u/3  %s",level,(r->discovered&(1u<<id))?"已发现":"未发现");text_cn(27,122,t,12,cyan);
  cp_rogue_effect(id,level,t,sizeof(t));text_cn(27,148,t,12,ink);text_cn(27,167,cp_relic_info[id].line2,12,muted);
  snprintf(t,sizeof(t),"发现 %u/28  共鸣 %u",(unsigned)__builtin_popcount(r->discovered),(unsigned)__builtin_popcount(synergy));text_cn(22,207,t,12,cyan);
  if(synergy){unsigned n=0;for(unsigned k=0;k<8&&n<2;++k)if(synergy&(1u<<k)){text_cn(22,229+(int)n*17,cp_synergy_names[k],12,gold);++n;}}
  text_cn(24,276,"上下浏览全部二十八种强化",12,ink);text_cn(24,295,"确定返回",12,muted);
 }else if(r->view==CP_VIEW_SHOP){
  snprintf(t,sizeof(t),"持有筹码 %lu",(unsigned long)g->score);text_cn(22,63,t,20,cyan);
  text_cn(22,103,"购买消耗筹码会降低排名",12,muted);
  snprintf(t,sizeof(t),"%u筹码 换五次投币",cp_rogue_price(g));rect(18,137,204,42,r->cursor?panel:color(46,59,63));text_cn(27,151,t,12,r->cursor?ink:gold);
  snprintf(t,sizeof(t),"本局已购 %u/%u",r->shop_uses,7+r->relic[CP_R_SHOP]);text_cn(25,193,t,12,cyan);
  if(g->score<cp_rogue_price(g))text_cn(25,215,"筹码不足",12,gold);
  else if(r->shop_uses>=7+r->relic[CP_R_SHOP])text_cn(25,215,"本局兑换次数已用完",12,gold);
  rect(18,242,204,29,r->cursor?color(46,59,63):panel);text_cn(30,250,"返回机台菜单",12,r->cursor?gold:ink);
  text_cn(24,294,"上下选择 确定兑换",12,muted);
 }else{
  text_cn(23,63,g->tactics.won?"五幕加冕 皇冠已入袋":g->tactics.end_reason==2?"本幕时间耗尽":"本局结算",12,cyan);
  snprintf(t,sizeof(t),"%lu",(unsigned long)g->score);text_cn(25,88,t,20,gold);
  snprintf(t,sizeof(t),"等级 %u  投币 %lu",r->level,(unsigned long)g->drops);text_cn(25,126,t,12,ink);
  snprintf(t,sizeof(t),"种子 %08lX",(unsigned long)r->seed);text_cn(25,150,t,12,muted);
  snprintf(t,sizeof(t),"获得强化 %u  组合共鸣 %u",owned,(unsigned)__builtin_popcount(r->synergies));text_cn(25,174,t,12,cyan);
  snprintf(t,sizeof(t),"特殊币 %lu  接触联动 %lu",(unsigned long)g->tactics.casts,(unsigned long)g->tactics.links);text_cn(25,197,t,12,ink);
  text_cn(25,218,r->practice?"练习局 不计入本机排行":"每次重启开启全新一局",12,muted);
  rect(18,240,204,29,panel);text_cn(30,248,"确定 开启新的命运",12,gold);
  text_cn(24,281,"上键排行榜 下键查看组合",12,ink);text_cn(24,298,r->store_failed?"存储失败 成绩未保存":"排行与发现保存在本机",12,r->store_failed?gold:muted);
 }
}

static CP_DRAW_HOT CP_NOIPA void render_tile(const cp_game_t *g,int y0,uint16_t *rgb,uint16_t *depth,int battery,bool spi)

{
    uint32_t t0=draw_cycles(),t1,t2,t3;
    scene_context(false);s_rgb=rgb;s_depth=depth;s_y0=y0;s_reflection_phase=g->circulation?(g->steps/2)&31:0;
    const int full_y=y0;uint16_t *const full_rgb=rgb,*const full_depth=depth;
    int scene_end=y0+s_tile_h;if(scene_end>274)scene_end=274;
    int scene_begin=y0<53?53:y0;
    if(scene_end<=scene_begin||(g->rogue.active&&g->rogue.view!=CP_VIEW_PLAY)){t1=t2=t3=t0;goto hud;}
    /* Opaque HUD panels cover these rows; never shade hidden scene pixels. */
    rgb+=(scene_begin-y0)*240;depth+=(scene_begin-y0)*240;y0=scene_begin;
    s_y0=y0;s_tile_h=scene_end-scene_begin;s_rgb=rgb;s_depth=depth;
    if(!base_region(y0,rgb,depth,s_native_x0,s_native_x1,y0,y0+s_tile_h)){for(unsigned i=0;i<240u*(unsigned)s_tile_h;++i){rgb[i]=color(13,22,29);depth[i]=65535;}}
    if(g->arcade.tower_ticks){int x0=g->arcade.tower_side?208:2,x1=g->arcade.tower_side?238:32;
        for(int y=y0;y<y0+s_tile_h;++y)if(y>=204&&y<270)for(int x=x0;x<x1;++x){if(x<s_native_x0||x>=s_native_x1)continue;
            unsigned at=(unsigned)(y-y0)*240+(unsigned)x;depth[at]=65535;rgb[at]=color(22,28,29);}}
    t1=draw_cycles();s_profile.base_us+=(t1-t0)/160;
    s_polygon_slot=0;s_slab_texture=5;s_material=s_colors?s_colors->steel:cp_palette_steel;
    slab(12*256,196*256,18*256,44*256+(int32_t)(g->pusher_y*256),18*256,4*256,color(132,157,165));
    s_slab_texture=-1;contact_shadows();s_polygon_slot=3;feeder_rod();
    if(g->guard_ticks){s_polygon_slot=(int)s_shape_count+6;
        quad((rv3){12*256,120*256,0},(rv3){18*256,120*256,0},(rv3){18*256,145*256,0},(rv3){12*256,145*256,0},color(63,191,121));
        quad((rv3){190*256,120*256,0},(rv3){196*256,120*256,0},(rv3){196*256,145*256,0},(rv3){190*256,145*256,0},color(63,191,121));
        quad((rv3){12*256,120*256,0},(rv3){12*256,145*256,0},(rv3){12*256,145*256,12*256},(rv3){12*256,120*256,12*256},color(51,147,103));
        quad((rv3){196*256,120*256,0},(rv3){196*256,145*256,0},(rv3){196*256,145*256,12*256},(rv3){196*256,120*256,12*256},color(51,147,103));
    }
    arcade_scene(g);
    uint32_t tc=draw_cycles();s_profile.scene_us+=(tc-t1)/160;
    for(unsigned order=0;order<s_order_count;++order){unsigned i=s_order[order];
        const cp_coin_t *b=&g->coins[i];
    if(!b->active||b->p.z<-20) continue;
        mesh_t *m=&s_mesh[i];
        if(m->ymax<s_y0||m->ymin>=s_y0+s_tile_h||m->xmax<s_clip_x0||m->xmin>=s_clip_x1)continue;
#ifndef CP_RENDER_BAKE_COIN
        if(!cp_kind_prize(b->kind)&&!s_baking_coin)coin_impostor(m);else
#endif
        mesh_raster(i,b);


    }
    s_profile.coins_us+=(draw_cycles()-tc)/160;
    if(g->tactics.active){
        static const uint32_t runes[8]={0x04eabe4,0x1155551,0x0457d44,0x046fe44,0x04aeea4,0x0e471c4,0x1f8c544,0x0e8ba2e};
        for(unsigned i=0;i<CP_MAX_COINS;++i){const cp_coin_t *b=&g->coins[i];if(!b->active||(!b->special&&!(b->trait&(CP_TRAIT_CROWN|CP_TRAIT_DOUBLE))))continue;
            point_t p=s_mesh[i].center;
            if(cp_kind_prize(b->kind))p=project((rv3){(int32_t)(b->p.x*256),(int32_t)(b->p.y*256),(int32_t)((b->p.z+cp_body_half(b).z)*256)});
            else{--p.y;p.z-=14;}
            uint32_t bits=b->trait&CP_TRAIT_CROWN?0x1157fe0:b->special?runes[b->special-1]:0x0a4e4ea;
            uint16_t tint=b->trait&CP_TRAIT_CROWN?color(255,245,180):b->trait&CP_TRAIT_DOUBLE?color(238,160,255):(b->trait&CP_TRAIT_FIRED)?color(140,161,158):color(65,236,226);
            for(int dy=-2;dy<=2;++dy)for(int dx=-2;dx<=2;++dx){if(!(bits&(1u<<((dy+2)*5+dx+2))))continue;int x=p.x+dx,y=p.y+dy;
                if(y<s_y0||y>=s_y0+s_tile_h||x<s_clip_x0||x>=s_clip_x1)continue;
                unsigned at=(unsigned)(y-s_y0)*240+(unsigned)x;
                if(abs((int)s_depth[at]-p.z)<55)s_rgb[at]=tint;
            }
        }
    }
    t2=draw_cycles();s_profile.faces_us+=(t2-t1)/160;
    /* Native 240x320 geometry and depth: preserve single-pixel rims and
     * stamped detail. Convert only the requested LCD region after shading. */
    if(spi){typedef uint32_t alias_u32 __attribute__((__may_alias__));
        for(int y=0;y<s_tile_h;++y){uint16_t *p=rgb+y*240+s_native_x0;int n=s_native_x1-s_native_x0;
            if(((uintptr_t)p&3)&&n){*p=wire_color(*p);++p;--n;}
            while(n>=2){uint32_t word=*(alias_u32 *)p;*(alias_u32 *)p=((word&0x00ff00ffu)<<8)|((word>>8)&0x00ff00ffu);p+=2;n-=2;}
            if(n)*p=wire_color(*p);
        }
    }
    t3=draw_cycles();s_profile.expand_us+=(t3-t2)/160;
hud:
    scene_context(false);y0=full_y;rgb=full_rgb;depth=full_depth;s_y0=y0;s_rgb=rgb;s_depth=depth;s_ui_spi=spi;
    uint16_t white=color(224,233,231),gold=color(242,194,87);
    char str[48];
    if(!g->rogue.active&&y0<53){rect(0,0,240,53,color(13,16,17));text_cn(18,4,"金币推土机",20,gold);
        snprintf(str,sizeof(str),"金币 %lu  收益 %lu",(unsigned long)g->bank,(unsigned long)g->score);text_cn(18,27,str,12,white);
        snprintf(str,sizeof(str),"亮灯通道 %u",g->arcade.lane+1);text_cn(18,40,str,12,gold);
        text_cn(112,40,g->arcade.spin_ticks?"转盘开奖":"命中抽奖",12,white);
        for(unsigned k=0;k<3;++k)rect(204+(int)k*9,43,5,5,(g->collection_mask&(1u<<k))?gold:color(53,61,62));
        rect(16,52,208,1,color(166,118,48));
        if(battery>=0){snprintf(str,sizeof(str),"%d%%",battery);text_cn(196,10,str,12,gold);}
    }
    if(!g->rogue.active&&y0+s_tile_h>274){rect(0,274,240,46,color(13,16,17));
        unsigned event=g->event_ticks?g->event_kind:g->arcade.hole_ticks?CP_EVENT_BLACKHOLE:g->arcade.double_ticks?CP_EVENT_DOUBLE:g->arcade.fever_ticks?CP_EVENT_FEVER:g->arcade.shake_ticks?CP_EVENT_SHAKE:g->arcade.tower_ticks?CP_EVENT_TOWERS:g->bonus_queue?CP_EVENT_RAIN:g->rush_ticks?CP_EVENT_RUSH:g->guard_ticks?CP_EVENT_SAVE:0;
        if(event==CP_EVENT_JACKPOT)text_cn(18,276,"超级大奖 十二枚奖励金币",12,gold);
        else if(event==CP_EVENT_BLACKHOLE)text_cn(18,276,"黑洞虹吸 全盘回收",12,color(200,158,255));
        else if(event==CP_EVENT_DOUBLE)text_cn(18,276,"金币增殖 全盘翻倍",12,gold);
        else if(event==CP_EVENT_TOWERS)text_cn(18,276,"双塔驾到 两侧推入",12,gold);
        else if(event==CP_EVENT_SHAKE)text_cn(18,276,"地震摇奖 撼动币盘",12,gold);
        else if(event==CP_EVENT_FEVER)text_cn(18,276,"狂热时刻 入袋双倍",12,gold);
        else if(event==CP_EVENT_RUSH){snprintf(str,sizeof(str),"推板加速 %u 秒",(g->rush_ticks+29)/30);text_cn(18,276,str,12,gold);}
        else if(event==CP_EVENT_RAIN){snprintf(str,sizeof(str),"金币雨 %u",g->bonus_queue);text_cn(18,276,str,12,gold);}
        else if(event==CP_EVENT_SAVE){snprintf(str,sizeof(str),"边槽保护 %u 秒",(g->guard_ticks+29)/30);text_cn(18,276,str,12,gold);}
        else if(g->circulation)text_cn(18,276,"循环推送 金币继续入袋",12,gold);
        else{snprintf(str,sizeof(str),"奖品 %lu  补货 %lu",(unsigned long)g->prizes,(unsigned long)g->restocks);text_cn(18,276,str,12,gold);}
        text_cn(24,291,g->paused?"长按下键重新开局":s_previous.full?"满台 等待推板":g->autoplay?"自动投币 上键停止":"确定投币 上键自动",12,white);
        text_cn(24,306,g->paused?"长按确定继续":"下键连投 长按确定暂停",12,white);
    }
    if(g->circulation){uint16_t indicator=(g->steps/10)&1?color(126,231,180):color(66,132,100);rect(20,255,6,2,indicator);rect(22,257,2,5,indicator);rect(21,262,4,2,indicator);}
    if(g->payout_flash>0)text_cn(96,256,"入袋",12,gold);
    if(!g->rogue.active||g->rogue.view==CP_VIEW_PLAY)arcade_marquee(g);
    if(g->arcade.spin_ticks||g->arcade.reveal_ticks){
        rect(44,91,152,54,color(9,23,28));rect(44,91,152,1,color(187,147,65));rect(44,144,152,1,color(187,147,65));
        rect(44,93,1,49,color(77,103,100));rect(195,93,1,49,color(77,103,100));
        text_cn(96,94,"命运轮盘",12,color(131,213,212));
        const char *name=g->arcade.spin_ticks?"能量汇聚":event_name(g->arcade.result);text_cn(80,109,name,20,gold);
        if(g->arcade.spin_ticks){for(unsigned k=0;k<18;++k){unsigned n=(k+g->steps/2)%18;rect(51+(int)k*8,137,5,2,n<5?gold:color(43,66,69));}}
        else {const char *detail=event_detail(g->arcade.result);text_cn(54,132,detail,12,white);}
    }
    if(g->arcade.fever_ticks){rect(18,220,2,53,gold);rect(220,220,2,53,gold);}
    if(g->paused){rect(64,147,112,30,color(16,25,32));text_cn(90,152,"暂停中",20,gold);}
    if(g->event_kind==CP_EVENT_JACKPOT&&g->event_ticks>2*CP_HZ){
        rect(18,55,2,217,gold);rect(220,55,2,217,gold);
        rect(53,163,134,46,color(28,21,12));text_cn(80,165,"皇家宝库",20,gold);text_cn(78,190,"金币瀑布与护航",12,white);
        for(unsigned k=0;k<10;++k){int x=28+(int)((k*37+g->steps*2)%184),y=62+(int)((k*53+g->steps*3)%200);
            rect(x-1,y,3,1,white);rect(x,y-1,1,3,gold);}
    }
    rogue_overlay(g);
    if(g->rogue.active&&g->rogue.view==CP_VIEW_PLAY&&battery>=0){snprintf(str,sizeof(str),"%d%%",battery);text_cn(196,10,str,12,gold);}
    /* Match the board's existing rounded black mask. */
    for(int yy=y0;yy<y0+s_tile_h;++yy) {if(yy>=30&&yy<=289)continue;for(int side=0;side<2;++side)for(int j=0;j<30;++j) {int xx=side?239-j:j;
        int dx=xx<30?30-xx:xx>209?xx-209:0,dy=yy<30?30-yy:yy>289?yy-289:0;
        if(dx&&dy&&dx*dx+dy*dy>900) rgb[(yy-y0)*240+xx]=0;
    }}
    s_profile.hud_us+=(draw_cycles()-t3)/160;
}

void cp_render_tile(const cp_game_t *g,int y0,uint16_t *rgb,uint16_t *depth,int battery){s_requested_h=CP_TILE_H;s_native_x0=0;s_native_x1=240;render_tile(g,y0,rgb,depth,battery,false);}
void cp_render_spi_tile(const cp_game_t *g,int y0,uint16_t *rgb,uint16_t *depth,int battery){s_requested_h=CP_TILE_H;s_native_x0=0;s_native_x1=240;render_tile(g,y0,rgb,depth,battery,true);}

void cp_render_spi_region(const cp_game_t *g,int y0,uint16_t *rgb,uint16_t *depth,int battery,int x0,int x1){s_requested_h=CP_TILE_H;s_native_x0=x0;s_native_x1=x1;render_tile(g,y0,rgb,depth,battery,true);}

void cp_render_spi_area(const cp_game_t *g,int y0,int height,uint16_t *rgb,uint16_t *depth,int battery,int x0,int x1){
 if(height<1||height>CP_TILE_H||y0<0||y0+height>CP_SCREEN_H)return;
 s_requested_h=height;s_native_x0=x0;s_native_x1=x1;render_tile(g,y0,rgb,depth,battery,true);s_requested_h=CP_TILE_H;
}
void cp_render_pack_area(uint16_t *rgb,int x0,int x1,unsigned height){
    if(!x0&&x1==240)return;
    typedef uint32_t alias_u32 __attribute__((__may_alias__));unsigned width=(unsigned)(x1-x0);
    if(((uintptr_t)rgb&3)==0){alias_u32 *words=(alias_u32 *)rgb;unsigned n=width/2;
        for(unsigned row=0;row<height;++row){alias_u32 *src=words+row*120+(unsigned)x0/2,*dst=words+row*n;for(unsigned col=0;col<n;++col)dst[col]=src[col];}
    }else{for(unsigned row=0;row<height;++row){uint16_t *src=rgb+row*240+(unsigned)x0,*dst=rgb+row*width;for(unsigned col=0;col<width;++col)dst[col]=src[col];}}
}

void cp_render_pack_region(uint16_t *rgb,int x0,int x1){cp_render_pack_area(rgb,x0,x1,CP_TILE_H);}
