#include "coin_save.h"
#include <string.h>
#include <stddef.h>
_Static_assert(sizeof(cp_save_t)<9216,"Compact orientation snapshot must stay below 9 KB");
static int16_t pack(float x,float scale) { return (int16_t)lroundf(cp_clamp(x*scale,-32767,32767)); }
static uint32_t checksum(const cp_save_t *s) {
    const uint8_t *p=(const uint8_t *)s;uint32_t h=2166136261u;
    for(size_t i=sizeof(s->checksum);i<sizeof(*s);++i) h=(h^p[i])*16777619u;
    return h;
}
void cp_save_encode(cp_save_t *s,const cp_game_t *g) {
    memset(s,0,sizeof(*s));s->format=9;
    s->rng=g->rng;s->bank=g->bank;s->score=g->score;s->best=g->best;s->drops=g->drops;
    s->collected=g->collected;s->lost=g->lost;s->prizes=g->prizes;s->round=g->round;
    s->phase=g->phase;s->game_time=g->game_time;
    for(unsigned i=0;i<CP_MAX_COINS;++i) {
        const cp_coin_t *b=&g->coins[i];cp_saved_coin_t *c=&s->coins[i];if(!b->active) continue;
        c->p[0]=pack(b->p.x,16);c->p[1]=pack(b->p.y,16);c->p[2]=pack(b->p.z,16);
        c->v[0]=pack(b->v.x,16);c->v[1]=pack(b->v.y,16);c->v[2]=pack(b->v.z,16);
        c->q[0]=pack(b->q.w,32767);c->q[1]=pack(b->q.x,32767);c->q[2]=pack(b->q.y,32767);c->q[3]=pack(b->q.z,32767);
        c->w[0]=pack(b->w.x,256);c->w[1]=pack(b->w.y,256);c->w[2]=pack(b->w.z,256);
        c->kind=b->kind;c->active=(uint8_t)(1u|(b->sleeping?2u:0u)|((unsigned)b->feeder<<2));c->feeder_mask=b->feeder_mask;
    }
    s->rod_hits=g->rod_hits;s->peg_hits=g->peg_hits;s->feeder_exits=g->feeder_exits;
    memcpy(s->exit_bins,g->exit_bins,sizeof(s->exit_bins));
    s->events=g->events;s->jackpots=g->jackpots;s->bonus_drops=g->bonus_drops;s->restocks=g->restocks;s->rush_ticks=g->rush_ticks;s->guard_ticks=g->guard_ticks;s->event_ticks=g->event_ticks;s->bonus_clock=g->bonus_clock;s->bonus_queue=g->bonus_queue;s->arcade=g->arcade;s->collection_mask=g->collection_mask;s->event_kind=g->event_kind;s->features=g->features;s->circulation=g->circulation;s->capacity_waits=g->capacity_waits;s->circulation_cycles=g->circulation_cycles;
    memcpy(s->restock_ticks,g->restock_ticks,sizeof(s->restock_ticks));
    s->checksum=checksum(s);
}
bool cp_save_decode(cp_game_t *g,const cp_save_t *s) {
    if(s->format!=9||s->checksum!=checksum(s)) return false;
    /* Validate the entire record before mutating the live cabinet. A valid
     * checksum alone does not make its body types/orientations valid. */
    if(!s->rng||s->bank>10000000||!isfinite(s->phase)||!isfinite(s->game_time))return false;
    if(s->features>1||s->circulation>1||s->rush_ticks>10*CP_HZ||s->guard_ticks>15*CP_HZ||s->event_ticks>4*CP_HZ||s->bonus_clock>CP_HZ||s->bonus_queue>4096||s->collection_mask>7||s->event_kind>CP_EVENT_MAX)return false;
    if(!cp_arcade_valid(&s->arcade))return false;
    for(unsigned k=0;k<3;++k)if(s->restock_ticks[k]>6*CP_HZ)return false;
    for(unsigned i=0;i<CP_MAX_COINS;++i){const cp_saved_coin_t *c=&s->coins[i];if(!c->active)continue;
        if(((c->active&~15u)||!(c->active&1u)||(c->active>>2)>2)||c->kind>CP_CHIP)return false;
        for(unsigned k=0;k<3;++k)if(c->p[k]<-16000||c->p[k]>16000)return false;
        int64_t norm=0;for(unsigned k=0;k<4;++k)norm+=(int64_t)c->q[k]*c->q[k];
        if(norm<966308660||norm>1181043918)return false;
    }
    memset(g,0,sizeof(*g));g->rng=s->rng;g->bank=s->bank;g->score=s->score;g->best=s->best;
    g->drops=s->drops;g->collected=s->collected;g->lost=s->lost;g->prizes=s->prizes;g->round=s->round;
    g->phase=s->phase;g->game_time=s->game_time;
    g->events=s->events;g->jackpots=s->jackpots;g->bonus_drops=s->bonus_drops;g->restocks=s->restocks;g->rush_ticks=s->rush_ticks;g->guard_ticks=s->guard_ticks;g->event_ticks=s->event_ticks;g->bonus_clock=s->bonus_clock;g->bonus_queue=s->bonus_queue;g->arcade=s->arcade;g->collection_mask=s->collection_mask;g->event_kind=s->event_kind;g->features=s->features;g->circulation=s->circulation;g->capacity_waits=s->capacity_waits;g->circulation_cycles=s->circulation_cycles;
    memcpy(g->restock_ticks,s->restock_ticks,sizeof(g->restock_ticks));
    g->pusher_y=8+17.5f*(1-cosf(g->phase));
    g->rod_hits=s->rod_hits;g->peg_hits=s->peg_hits;g->feeder_exits=s->feeder_exits;
    memcpy(g->exit_bins,s->exit_bins,sizeof(g->exit_bins));
    for(unsigned i=0;i<CP_MAX_COINS;++i) {
        const cp_saved_coin_t *c=&s->coins[i];cp_coin_t *b=&g->coins[i];if(!c->active) continue;
        b->active=true;b->sleeping=(c->active&2)!=0;b->kind=c->kind;b->feeder=c->active>>2;b->feeder_mask=c->feeder_mask;
        b->p=cp_v(c->p[0]/16.0f,c->p[1]/16.0f,c->p[2]/16.0f);
        b->v=cp_v(c->v[0]/16.0f,c->v[1]/16.0f,c->v[2]/16.0f);
        b->w=cp_v(c->w[0]/256.0f,c->w[1]/256.0f,c->w[2]/256.0f);
        b->q=(cp_q4){c->q[0]/32767.0f,c->q[1]/32767.0f,c->q[2]/32767.0f,c->q[3]/32767.0f};
        cp_body_refresh(b);
    }
    if(!cp_valid(g)) return false;
    cp_rebuild_support(g);
    return true;
}
