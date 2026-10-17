#include "pusher_tactics.h"
#include "pusher_physics.h"
#include <string.h>
#include <stdlib.h>
#define COLD __attribute__((noinline))
const cp_special_info_t cp_special_info[CP_SPECIAL_COUNT]={
 {"震荡币","落稳后震开附近币堆","碰到棱镜可扩大震荡","震"},
 {"磁核币","落盘吸引附近金币六秒","遇重锤产生一次磁暴","磁"},
 {"萌芽币","与潮汐币接触后生长","从盘面补出六枚金币","芽"},
 {"潮汐币","落盘后保留八秒活性","靠近萌芽汇流并接触生长","水"},
 {"棱镜币","接触普通币使价值翻倍","可强化震荡币与磁核","镜"},
 {"重锤币","双倍质量砸动币堆","碰到磁核产生磁暴","锤"},
 {"护航币","落盘后开启边槽保护","适合在连锁爆发前使用","护"},
 {"回响币","落盘后补发三枚金币","免费币仍经过摆杆钉阵","响"}
};
static unsigned roll(cp_game_t *g){uint32_t x=g->rng;x^=x<<13;x^=x>>17;x^=x<<5;return g->rng=x?x:12345;}
static bool inside(const cp_coin_t *b){return b->active&&!b->feeder&&b->p.x>=12&&b->p.x<=196&&b->p.y>=82&&b->p.y<=CP_FRONT_EDGE&&b->p.z>=0;}
static void notice(cp_game_t *g,unsigned kind,const cp_coin_t *b){cp_tactics_t *t=&g->tactics;t->fx_kind=(uint8_t)kind;t->fx_ticks=24;t->fx_x=(int16_t)b->p.x;t->fx_y=(int16_t)b->p.y;++g->rogue.revision;}
void cp_tactics_grant(cp_game_t *g,unsigned kind,unsigned count){
 if(kind<1||kind>CP_SPECIAL_COUNT)return;
 cp_tactics_t *t=&g->tactics;
 while(count--){if(t->stock[kind-1]>=CP_SPECIAL_STOCK||t->queue_count>=CP_SPECIAL_QUEUE){if(g->bonus_queue<4096)++g->bonus_queue;continue;}
  t->queue[(t->queue_head+t->queue_count)%CP_SPECIAL_QUEUE]=(uint8_t)kind;++t->queue_count;++t->stock[kind-1];}
 ++g->rogue.revision;
}
void cp_tactics_start(cp_game_t *g){
 memset(&g->tactics,0,sizeof(g->tactics));g->tactics.active=true;
 cp_tactics_grant(g,CP_SP_SEED,1);cp_tactics_grant(g,CP_SP_WATER,1);cp_tactics_grant(g,CP_SP_PRISM,1);cp_tactics_grant(g,CP_SP_BLAST,1);
}
unsigned cp_tactics_goal(const cp_game_t *g){static const unsigned goals[CP_ACT_COUNT]={450,750,1100,1550,2200};unsigned goal=goals[g->tactics.stage<CP_ACT_COUNT?g->tactics.stage:CP_ACT_COUNT-1];return g->tactics.contract==2?goal*5/4:goal;}
unsigned cp_tactics_earned(const cp_game_t *g){return g->rogue.gross>=g->tactics.stage_gross?g->rogue.gross-g->tactics.stage_gross:0;}
unsigned cp_tactics_quota(const cp_game_t *g){static const unsigned quota[CP_ACT_COUNT]={55,85,110,140,170};return quota[g->tactics.stage<CP_ACT_COUNT?g->tactics.stage:CP_ACT_COUNT-1];}
unsigned cp_tactics_seconds(const cp_game_t *g){static const unsigned limits[CP_ACT_COUNT]={180,210,240,270,300};unsigned limit=limits[g->tactics.stage<CP_ACT_COUNT?g->tactics.stage:CP_ACT_COUNT-1]*CP_HZ;return (limit-(g->tactics.stage_ticks<limit?g->tactics.stage_ticks:limit)+CP_HZ-1)/CP_HZ;}
/* App-only dense load; mechanical fixtures retain cp_init's original setup. */
void cp_cabinet_load(cp_game_t *g){
 /* Retain the original towers and prizes; pack the surrounding playable bed
  * with staggered rows instead of hiding the extra stock under the ramp. */
 for(unsigned i=0;i<CP_MAX_COINS;++i){cp_coin_t *b=&g->coins[i];if(!b->active||b->kind!=CP_COIN_GOLD)continue;
  bool tower=(hypotf(b->p.x-63,b->p.y-111)<11||hypotf(b->p.x-146,b->p.y-111)<11)&&b->p.z<16;
  if(!tower)b->active=false;
 }
 for(unsigned pass=0;pass<2;++pass)for(unsigned layer=0;layer<(pass?16u:1u);++layer)
  for(unsigned row=0;row<(pass?3u:1u);++row)for(unsigned col=0;col<(pass?10u:4u);++col){
   if(cp_coin_count(g)>=CP_INITIAL_COINS)return;
   float x=pass?22+col*18+(row&1?8:0):31+col*48;
   float y=pass?100+row*16.5f:26+row*18,z=pass?1.3f+layer*2.6f:19.3f;
   if(x>188)continue;
   bool clear=cp_placement_clear(g,CP_COIN_GOLD,cp_v(x,y,z));int support=-1;float nearest=1000;
   for(unsigned i=0;i<CP_MAX_COINS;++i){const cp_coin_t *b=&g->coins[i];if(!b->active)continue;
    if(pass&&layer&&b->kind==CP_COIN_GOLD&&fabsf(b->p.z-(z-2.6f))<.1f){float d=hypotf(b->p.x-x,b->p.y-y);if(d<.1f&&d<nearest){nearest=d;support=(int)i;}}
   }
   if(!clear||(pass&&layer&&support<0))continue;
   cp_coin_t *b=cp_spawn(g,CP_COIN_GOLD,cp_v(x,y,z),(cp_q4){1,0,0,0});
   if(b){b->sleeping=true;b->support_count=1;b->support[0]=(int16_t)support;}
  }
}
unsigned cp_tactics_relic_coin(unsigned relic){
 static const uint8_t theme[CP_RELIC_COUNT]={2,6,4,1,8,6,8,1,2,3,8,5,5,5,7,5,5,8,8,7,7,6,3,4,1,2,6,5};
 return relic<CP_RELIC_COUNT?theme[relic]:0;
}
void cp_tactics_upgrade(cp_game_t *g,unsigned relic){
 if(g->tactics.active)cp_tactics_grant(g,cp_tactics_relic_coin(relic),1);
}
bool cp_tactics_cast(cp_game_t *g,unsigned kind){
 cp_tactics_t *t=&g->tactics;
 if(!t->active||g->rogue.finished||g->rogue.view!=CP_VIEW_PLAY||!t->queue_count||kind!=t->queue[t->queue_head])return false;
 if(!cp_special_inlet(g,kind)){t->blocked_ticks=45;++g->rogue.revision;return false;}
 --t->stock[kind-1];t->queue_head=(uint8_t)((t->queue_head+1)%CP_SPECIAL_QUEUE);--t->queue_count;
 ++t->casts;++t->used[kind-1];++g->rogue.revision;return true;
}
unsigned cp_tactics_auto_kind(const cp_game_t *g){return g->tactics.queue_count?g->tactics.queue[g->tactics.queue_head]:0;}
/* Called only after a real non-speculative SAT contact, before impulses.
 * Flags defer every spawn/force until the complete solver has finished. */
COLD void cp_tactics_touch(cp_game_t *g,unsigned ia,unsigned ib){
 if(!g->tactics.active||g->paused)return;
 cp_coin_t *a=&g->coins[ia],*b=&g->coins[ib];if(a->feeder||b->feeder)return;
 for(unsigned side=0;side<2;++side){cp_coin_t *x=side?b:a,*y=side?a:b;
  if(x->trait&CP_TRAIT_FIRED)continue;
  if(x->special==CP_SP_SEED&&y->special==CP_SP_WATER&&!(x->trait&CP_TRAIT_WATERED)&&!(y->trait&CP_TRAIT_FIRED)){
   x->trait|=CP_TRAIT_WATERED;y->trait|=CP_TRAIT_FIRED;++g->tactics.links;notice(g,CP_SP_SEED,x);
  }
  if(x->special==CP_SP_PRISM&&y->kind<CP_PRIZE_RED&&!(y->trait&CP_TRAIT_DOUBLE)){
   y->trait|=CP_TRAIT_DOUBLE;
   if(y->special==CP_SP_BLAST||y->special==CP_SP_MAGNET){y->trait|=CP_TRAIT_CHARGED;++g->tactics.links;notice(g,CP_SP_PRISM,y);}
  }
  if(x->special==CP_SP_METEOR&&y->special==CP_SP_MAGNET&&!(x->trait&CP_TRAIT_CHARGED)&&!(y->trait&CP_TRAIT_FIRED)){
   x->trait|=CP_TRAIT_CHARGED;++g->tactics.links;notice(g,CP_SP_METEOR,x);
  }
 }
}
static COLD void burst(cp_game_t *g,unsigned source,unsigned radius){
 const cp_coin_t *a=&g->coins[source];
 for(unsigned i=0;i<CP_MAX_COINS;++i){cp_coin_t *b=&g->coins[i];if(!inside(b))continue;
  int dx=(int)((b->p.x-a->p.x)*256),dy=(int)((b->p.y-a->p.y)*256),dz=(int)((b->p.z-a->p.z)*256);
  if(abs(dx)>(int)radius*256||abs(dy)>(int)radius*256||abs(dz)>(int)radius*256)continue;
  if(dx*dx+dy*dy+dz*dz>(int)(radius*radius*65536u))continue;
  cp_body_kick(g,i,dx/3,dy/3+18*256,26*256);
 }
 ++g->tactics.blasts;notice(g,CP_SP_BLAST,a);
}
void cp_tactics_collect(cp_game_t *g,unsigned i){
 if(!g->tactics.active)return;
 cp_coin_t *b=&g->coins[i];
 if(b->trait&CP_TRAIT_CROWN){g->tactics.crown_collected=true;notice(g,CP_SP_GUARD,b);}
 if(cp_kind_prize(b->kind)&&++g->tactics.prize_stock>=2){g->tactics.prize_stock=0;cp_tactics_grant(g,1+roll(g)%CP_SPECIAL_COUNT,1);}
}
void cp_tactics_lost(cp_game_t *g,unsigned i){
 if(g->tactics.active&&(g->coins[i].trait&CP_TRAIT_CROWN)){g->tactics.crown_spawned=false;g->tactics.crown_retry=g->steps+8*CP_HZ;}
}
static COLD void grow(cp_game_t *g,unsigned i){
 cp_coin_t *seed=&g->coins[i];float z=seed->p.z+seed->bound.z+1.5f;
 if(cp_coin_count(g)>CP_MAX_COINS-6)return;
 for(unsigned k=0;k<CP_MAX_COINS;++k){const cp_coin_t *b=&g->coins[k];if(k!=i&&b->active&&fabsf(b->p.x-seed->p.x)<b->bound.x+8&&fabsf(b->p.y-seed->p.y)<b->bound.y+8&&b->p.z+b->bound.z>z-1.3f&&b->p.z-b->bound.z<z+14.3f)return;}
 for(unsigned k=0;k<6;++k){cp_coin_t *b=cp_spawn(g,CP_COIN_GOLD,cp_v(seed->p.x,seed->p.y,z+k*2.6f),(cp_q4){1,0,0,0});if(b){++g->tactics.grown;if(seed->trait&CP_TRAIT_DOUBLE)b->trait|=CP_TRAIT_DOUBLE;}}
 seed->trait|=CP_TRAIT_FIRED;notice(g,CP_SP_SEED,seed);
}
static COLD void physical(cp_game_t *g){
 for(unsigned i=0;i<CP_MAX_COINS;++i){cp_coin_t *b=&g->coins[i];if(!b->special||!inside(b)||(b->trait&CP_TRAIT_FIRED))continue;
  if(!b->special_ticks){if(!b->support_count)continue;b->special_ticks=(uint16_t)((b->special==CP_SP_BLAST?1:8)*CP_HZ);}
  if(b->special==CP_SP_BLAST){if(--b->special_ticks==0){burst(g,i,(b->trait&CP_TRAIT_CHARGED)?48:32);b->trait|=CP_TRAIT_FIRED;}continue;}
  if(b->special==CP_SP_GUARD){cp_reward_event(g,CP_EVENT_SAVE);b->trait|=CP_TRAIT_FIRED;notice(g,CP_SP_GUARD,b);continue;}
  if(b->special==CP_SP_ECHO){unsigned n=g->bonus_queue+3;g->bonus_queue=(uint16_t)(n>4096?4096:n);b->trait|=CP_TRAIT_FIRED;notice(g,CP_SP_ECHO,b);continue;}
  if(b->special==CP_SP_SEED){if(b->trait&CP_TRAIT_WATERED)grow(g,i);continue;}
  if(b->special==CP_SP_WATER&&g->steps%3==0){
   for(unsigned j=0;j<CP_MAX_COINS;++j){const cp_coin_t *seed=&g->coins[j];if(!inside(seed)||seed->special!=CP_SP_SEED||(seed->trait&(CP_TRAIT_FIRED|CP_TRAIT_WATERED)))continue;
    int dx=(int)((seed->p.x-b->p.x)*256),dy=(int)((seed->p.y-b->p.y)*256);if(abs(dx)>56*256||abs(dy)>56*256)continue;
    if(dx*dx+dy*dy>56*56*65536)continue;
    cp_body_kick(g,i,dx/8,dy/8,0);break;
   }
  }
  if(b->special==CP_SP_METEOR&&(b->trait&CP_TRAIT_CHARGED)){burst(g,i,42);b->trait|=CP_TRAIT_FIRED;continue;}
  if(b->special==CP_SP_MAGNET&&b->special_ticks>2*CP_HZ&&g->steps%3==0){
   for(unsigned j=0;j<CP_MAX_COINS;++j){cp_coin_t *c=&g->coins[j];if(j==i||!inside(c))continue;
    int dx=(int)((b->p.x-c->p.x)*256),dy=(int)((b->p.y-c->p.y)*256),dz=(int)((b->p.z-c->p.z)*256);
    if(abs(dx)>52*256||abs(dy)>52*256||abs(dz)>52*256)continue;
    if(dx*dx+dy*dy+dz*dz>52*52*65536)continue;
    cp_body_kick(g,j,dx/20,dy/20+2*256,0);if(b->trait&CP_TRAIT_CHARGED)c->trait|=CP_TRAIT_DOUBLE;
   }
  }
  if(!--b->special_ticks)b->trait|=CP_TRAIT_FIRED;
 }
}
static COLD void stage_clear(cp_game_t *g){
 cp_tactics_t *t=&g->tactics;
 if(t->stage==CP_ACT_COUNT-1){t->won=true;t->end_reason=1;cp_reward_event(g,CP_EVENT_JACKPOT);cp_rogue_finish(g);return;}
 ++t->stage;t->stage_ticks=0;t->stage_gross=g->rogue.gross;t->stage_collected=g->collected;t->contract=0;
 unsigned first=roll(g)%CP_SPECIAL_COUNT;
 for(unsigned k=0;k<3;++k)t->supply[k]=(uint8_t)(1+(first+k*3)%CP_SPECIAL_COUNT);
 if(t->stage==CP_ACT_COUNT-1){g->arcade.tower_queue=2;g->arcade.tower_clock=0;}
 cp_rogue_view(g,CP_VIEW_SUPPLY);
}
void cp_tactics_supply(cp_game_t *g,unsigned choice){
 if(g->rogue.view!=CP_VIEW_SUPPLY||choice>2)return;
 g->tactics.contract=(uint8_t)choice;
 unsigned credit=choice==0?12:choice==1?7:18;
 if(choice==0)cp_tactics_grant(g,CP_SP_GUARD,1);
 else if(choice==1){unsigned kind=cp_tactics_relic_coin(g->rogue.inspect);cp_tactics_grant(g,kind,1);cp_tactics_grant(g,kind==CP_SP_SEED?CP_SP_WATER:kind==CP_SP_MAGNET?CP_SP_METEOR:CP_SP_PRISM,1);}
 else {g->arcade.tower_queue=2;g->arcade.tower_clock=0;}
 g->bank=g->bank>999-credit?999:g->bank+credit;g->rogue.grace=0;
 cp_rogue_view(g,CP_VIEW_PLAY);
}
COLD void cp_tactics_tick(cp_game_t *g){
 cp_tactics_t *t=&g->tactics;if(!t->active||g->rogue.finished)return;
 physical(g);if(t->blocked_ticks)--t->blocked_ticks;if(t->fx_ticks)--t->fx_ticks;
 /* Stock is consumed only by the next ordinary input, never a separate timer. */
 t->autocast_ticks=0;
 if(t->hopper_clock)--t->hopper_clock;
 if(g->features&&g->arcade.play_ticks&&(g->bank||t->queue_count)&&cp_coin_count(g)<CP_HOPPER_TARGET&&!t->hopper_clock){
  for(unsigned k=0;k<10;++k){int x=24+(int)((g->steps+k*3)%10)*18;if(cp_house_coin(g,x,30)){++t->house_coins;break;}}
  t->hopper_clock=36;
 }
 if(cp_tactics_seconds(g))++t->stage_ticks;
 if(g->rogue.view!=CP_VIEW_PLAY)return;
 if(cp_tactics_earned(g)>=cp_tactics_goal(g)&&g->collected-t->stage_collected>=cp_tactics_quota(g)&&(t->stage!=CP_ACT_COUNT-1||t->crown_collected)){stage_clear(g);return;}
 if(t->stage==CP_ACT_COUNT-1&&!t->crown_spawned&&g->steps>=t->crown_retry&&!g->arcade.tower_queue&&!g->arcade.tower_ticks)g->arcade.tower_queue=1;
 if(!cp_tactics_seconds(g)){t->end_reason=2;cp_rogue_finish(g);}
}
bool cp_tactics_valid(const cp_tactics_t *t){
 if(!t->active)return true;
 if(t->blocked_ticks>45||t->stage>=CP_ACT_COUNT||t->selected>=CP_SPECIAL_COUNT||t->fx_kind>CP_SPECIAL_COUNT||t->fx_ticks>24||t->end_reason>2||t->stage_ticks>300*CP_HZ+1||t->autocast_ticks>5*CP_HZ)return false;
 if(t->queue_head>=CP_SPECIAL_QUEUE||t->queue_count>CP_SPECIAL_QUEUE||t->contract>2||t->prize_stock>1)return false;
 unsigned inventory[CP_SPECIAL_COUNT]={0};for(unsigned k=0;k<t->queue_count;++k){unsigned id=t->queue[(t->queue_head+k)%CP_SPECIAL_QUEUE];if(id<1||id>CP_SPECIAL_COUNT)return false;++inventory[id-1];}
 for(unsigned k=0;k<CP_SPECIAL_COUNT;++k)if(t->stock[k]>CP_SPECIAL_STOCK||t->stock[k]!=inventory[k])return false;
 for(unsigned k=0;k<3;++k)if(t->supply[k]>CP_SPECIAL_COUNT)return false;
 return true;
}
