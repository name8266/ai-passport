#include "coin_save.h"
#include <math.h>
#include <string.h>
_Static_assert(sizeof(cp_save_t)<4000,"Snapshot must fit one NVS data page");
static int16_t pack(float v) {
    if (!isfinite(v)) return 0;
    v=fmaxf(-2047.0f,fminf(2047.0f,v));
    return (int16_t)lroundf(v*16.0f);
}
static uint32_t checksum(const cp_save_t *s) {
    const uint8_t *p=(const uint8_t *)s;
    uint32_t h=2166136261u;
    for(size_t i=sizeof(s->checksum);i<sizeof(*s);++i) h=(h^p[i])*16777619u;
    return h;
}
void cp_save_encode(cp_save_t *s,const cp_game_t *g) {
    memset(s,0,sizeof(*s));
    memcpy(s->meta,&g->rng,sizeof(s->meta));
    for(unsigned i=0;i<CP_MAX_COINS;++i) {
        const cp_coin_t *c=&g->coins[i]; cp_saved_coin_t *d=&s->coins[i];
        if(!c->active) continue;
        *d=(cp_saved_coin_t){pack(c->x),pack(c->y),pack(c->z),
            pack(c->vx),pack(c->vy),pack(c->vz),(uint8_t)c->kind,
            (uint8_t)(1u|(c->falling?2u:0u))};
    }
    for(unsigned i=0;i<CP_MAX_BALLS;++i) {
        const cp_ball_t *b=&g->balls[i]; cp_saved_ball_t *d=&s->balls[i];
        if(b->active) *d=(cp_saved_ball_t){pack(b->x),pack(b->y),
            pack(b->vx),pack(b->vy),(uint8_t)b->kind,1};
    }
    memcpy(s->slot_hits,g->slot_hits,sizeof(s->slot_hits)); s->peg_hits=g->peg_hits;
    s->checksum=checksum(s);
}
bool cp_save_decode(cp_game_t *g,const cp_save_t *s) {
    if(s->checksum!=checksum(s)) return false;
    memset(g,0,sizeof(*g)); memcpy(&g->rng,s->meta,sizeof(s->meta));
    if(!g->rng || g->event>CP_EVENT_RAPID_FIRE || !isfinite(g->game_time) ||
       !isfinite(g->phase) || !isfinite(g->drop_x) || !isfinite(g->pusher_y) ||
       !isfinite(g->boost_left) || !isfinite(g->event_left)) return false;
    for(unsigned i=0;i<CP_MAX_COINS;++i) {
        const cp_saved_coin_t *d=&s->coins[i]; cp_coin_t *c=&g->coins[i];
        if(!(d->flags&1)) continue;
        if(d->kind>CP_COIN_GIANT || d->flags>3) return false;
        *c=(cp_coin_t){.x=d->x/16.0f,.y=d->y/16.0f,.z=d->z/16.0f,
            .vx=d->vx/16.0f,.vy=d->vy/16.0f,.vz=d->vz/16.0f,
            .kind=(cp_coin_kind_t)d->kind,.active=true,.falling=(d->flags&2)!=0,
            .radius=d->kind==CP_COIN_GIANT?11.8f:8.2f,
            .height=d->kind==CP_COIN_GIANT?4.8f:2.9f,
            .mass=d->kind==CP_COIN_GIANT?2.5f:1.0f};
    }
    for(unsigned i=0;i<CP_MAX_BALLS;++i) {
        const cp_saved_ball_t *d=&s->balls[i];
        if(!d->active) continue;
        if(d->kind>CP_COIN_GIANT) return false;
        g->balls[i]=(cp_ball_t){.x=d->x/16.0f,.y=d->y/16.0f,
            .vx=d->vx/16.0f,.vy=d->vy/16.0f,.kind=(cp_coin_kind_t)d->kind,.active=true};
    }
    memcpy(g->slot_hits,s->slot_hits,sizeof(g->slot_hits));g->peg_hits=s->peg_hits;
    g->paused=false;
    return cp_coin_count(g)+cp_ball_count(g)<=CP_MAX_COINS;
}
