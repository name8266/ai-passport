#include "pusher_rogue.h"
#include "pusher_physics.h"
#include "pusher_records.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static cp_game_t g;
static void start(unsigned seed){cp_init(&g,seed,CP_RUN_COINS,0,1);cp_rogue_start(&g,0);}
static void play(void){while(g.rogue.view==CP_VIEW_DRAFT)cp_rogue_choose(&g,0);assert(!g.paused);}
static void empty(void){memset(g.coins,0,sizeof(g.coins));g.features=false;}
static void collect(unsigned n){for(unsigned k=0;k<n;++k)cp_rogue_collect(&g,CP_COIN_GOLD,false,104,145,1);}
static void grant(unsigned id,unsigned n){for(unsigned k=0;k<n;++k)cp_rogue_grant(&g,id);}
static void tick(unsigned n){for(unsigned k=0;k<n;++k){if(g.rogue.view==CP_VIEW_DRAFT)cp_rogue_choose(&g,0);if(g.rogue.view==CP_VIEW_SUPPLY)cp_tactics_supply(&g,0);cp_step(&g,CP_DT);
 if(!cp_valid(&g)){
  fprintf(stderr,"INVALID seed%u step%u time%f bank%u rogue%d arcade%d view%u level%u xp%u/%u rush%u guard%u hole%u double%u shake%u fever%u\n",g.rogue.seed,g.steps,g.game_time,g.bank,cp_rogue_valid(&g.rogue),cp_arcade_valid(&g.arcade),g.rogue.view,g.rogue.level,g.rogue.xp,g.rogue.goal,g.rush_ticks,g.guard_ticks,g.arcade.hole_ticks,g.arcade.double_ticks,g.arcade.shake_ticks,g.arcade.fever_ticks);
  for(unsigned j=0;j<CP_MAX_COINS;++j)if(g.coins[j].active){cp_coin_t *b=&g.coins[j];if(!isfinite(b->p.x)||!isfinite(b->p.y)||!isfinite(b->p.z)||fabsf(b->p.x)>1000||fabsf(b->p.y)>1000||fabsf(b->p.z)>1000)fprintf(stderr,"BODY %u kind%u pos%f,%f,%f\n",j,b->kind,b->p.x,b->p.y,b->p.z);}
  assert(0);
 }}}
static void economy(void){
 start(2);assert(g.bank==CP_RUN_COINS&&g.score==0&&g.paused&&g.rogue.view==CP_VIEW_DRAFT);
 assert(g.rogue.offer[0]!=g.rogue.offer[1]&&g.rogue.offer[1]!=g.rogue.offer[2]);
 uint32_t steps=g.steps;cp_step(&g,1);assert(g.steps==steps&&!cp_drop(&g));play();
 memset(g.rogue.relic,0,sizeof(g.rogue.relic));g.bank=0;collect(15);assert(g.bank==0&&g.score==15);collect(1);assert(g.bank==1&&g.score==16);
 cp_rogue_collect(&g,CP_CHIP,false,104,145,25);assert(g.bank==2&&g.score==41);
 cp_rogue_collect(&g,CP_PRIZE_RED,false,104,145,40);assert(g.bank==2&&g.score==81);
 unsigned score=g.score;assert(cp_rogue_buy(&g)&&g.bank==7&&g.score==score-32);g.score=10000;for(unsigned k=1;k<7;++k)assert(cp_rogue_buy(&g));assert(!cp_rogue_buy(&g));
 start(3);play();empty();g.bank=0;memset(g.tactics.stock,0,sizeof(g.tactics.stock));g.tactics.queue_count=0;tick(599);assert(!g.rogue.finished&&g.rogue.grace==599);tick(1);assert(g.paused&&g.rogue.finished&&g.rogue.view==CP_VIEW_END);
 assert(cp_rogue_key(&g,2,false)==CP_CMD_NEW&&g.bank==0); /* No silent free refill. */
 start(4);play();empty();grant(CP_R_INSURANCE,1);g.bank=0;memset(g.tactics.stock,0,sizeof(g.tactics.stock));g.tactics.queue_count=0;tick(600);assert(g.bank==5&&!g.rogue.finished&&!g.rogue.insurance);
 g.bank=0;memset(g.tactics.stock,0,sizeof(g.tactics.stock));g.tactics.queue_count=0;tick(600);assert(g.rogue.finished);
 start(5);play();empty();collect(20);cp_step(&g,.25f);assert(g.rogue.view==CP_VIEW_DRAFT&&g.paused&&g.accumulator==0);
 unsigned time=g.steps;cp_step(&g,1);assert(g.steps==time);cp_rogue_choose(&g,0);assert(!g.paused&&g.rogue.level==2);
}
static void chip_fever(void){
 start(92);play();empty();memset(g.rogue.relic,0,sizeof(g.rogue.relic));g.bank=0;g.arcade.fever_ticks=10;
 cp_spawn(&g,CP_CHIP,cp_v(104,145,-27),(cp_q4){1,0,0,0});cp_step(&g,CP_DT);assert(g.score==18&&g.bank==1&&g.collected==1&&g.prizes==0);
 start(93);play();empty();g.features=true;
 cp_coin_t *b=cp_spawn(&g,CP_CHIP,cp_v(104,110,10),(cp_q4){1,0,0,0});cp_reward_event(&g,CP_EVENT_DOUBLE);assert(!(b->feeder_mask&CP_BODY_CLONE));
}
static void idle_budget(void){
 start(66);play();empty();g.features=true;g.autoplay=false;
 /* No paid activity: queued free coins cannot keep mystery draws or restock alive. */
 g.bonus_queue=12;tick(600);assert(!g.arcade.spins&&!g.restocks&&!g.arcade.towers&&g.drops==0);
 unsigned saved=g.bank;cp_rogue_view(&g,CP_VIEW_PAUSE);tick(60);assert(g.bank==saved);
}
static void effects(void){
 start(17);play();memset(g.rogue.relic,0,sizeof(g.rogue.relic));
 for(unsigned id=0;id<CP_RELIC_COUNT;++id){grant(id,4);assert(g.rogue.relic[id]==3);assert(cp_relic_info[id].name&&cp_relic_info[id].line1&&cp_relic_info[id].line2);}
 assert(g.rogue.synergies==255&&g.rogue.gravity==79&&g.rogue.friction==64&&g.rogue.stroke==41&&g.rogue.bounce==86);
 for(unsigned lane=0;lane<3;++lane)assert(cp_rogue_lane(&g,lane));
 cp_rod_t plain,fast;cp_feeder_rod(.2f,&plain);cp_game_rod(&g,.2f,&fast);assert(plain.x!=fast.x||plain.z!=fast.z);
 cp_reward_event(&g,CP_EVENT_BLACKHOLE);assert(g.arcade.hole_ticks==326);cp_reward_event(&g,CP_EVENT_BLACKHOLE);assert(g.arcade.hole_ticks==326);cp_reward_event(&g,CP_EVENT_BLACKHOLE);assert(g.arcade.hole_ticks==326);
 unsigned prior=g.bonus_queue;for(unsigned k=0;k<30;++k)cp_rogue_paid(&g);assert(g.bonus_queue>prior);
 unsigned bank=g.bank;g.rogue.exchange_progress=0;cp_rogue_collect(&g,CP_CHIP,false,104,145,25);assert(g.bank>bank&&g.rogue.exchange_progress<80);
 g.rogue.fusion_progress=0;g.rogue.map_progress=0;prior=g.bonus_queue;collect(96);assert(g.bonus_queue>=prior+8&&g.rogue.prize_queue>0&&g.rogue.chip_queue>0&&g.rogue.ripple_ticks);
 for(unsigned k=0;k<6;++k)cp_rogue_event(&g);assert(g.rogue.storm_ticks>0);
 unsigned value=g.score;cp_rogue_reel(&g);assert(g.score>value&&g.rogue.float_ticks==60);
 prior=g.bonus_queue;for(unsigned k=0;k<100;++k)cp_rogue_lost(&g);assert(g.bonus_queue>prior&&g.rogue.chip_queue==4);
 g.rogue.pending=1;cp_rogue_view(&g,CP_VIEW_DRAFT);g.rogue.rerolls=3;assert(cp_rogue_reroll(&g)==true); /* All maxed exits selection safely. */
 assert(g.rogue.view==CP_VIEW_PLAY&&g.rogue.pending==0);
 start(18);play();grant(CP_R_FATE,3);collect(20);cp_rogue_tick(&g);assert(g.rogue.rerolls==3);assert(cp_rogue_reroll(&g)&&g.rogue.rerolls==2);assert(cp_rogue_reroll(&g)&&cp_rogue_reroll(&g)&&!cp_rogue_reroll(&g));
 cp_rogue_choose(&g,1);cp_rogue_key(&g,2,true);assert(g.rogue.view==CP_VIEW_PAUSE&&g.paused);
 cp_rogue_key(&g,1,false);cp_rogue_key(&g,2,false);assert(g.rogue.view==CP_VIEW_CODEX);cp_rogue_key(&g,0,false);assert(g.rogue.inspect==27);cp_rogue_key(&g,2,false);assert(g.rogue.view==CP_VIEW_PAUSE);
}
static void sound_controls(void){
 start(71);play();cp_rogue_key(&g,2,true);assert(g.paused&&g.rogue.view==CP_VIEW_PAUSE);
 for(unsigned i=0;i<4;++i)assert(cp_rogue_key(&g,1,false)==CP_CMD_NONE);
 assert(g.rogue.cursor==4&&!g.rogue.muted);unsigned revision=g.rogue.revision,steps=g.steps;
 assert(cp_rogue_key(&g,2,false)==CP_CMD_SOUND&&g.rogue.muted&&g.rogue.revision>revision);
 assert(g.rogue.view==CP_VIEW_PAUSE&&g.paused&&g.steps==steps&&!g.rogue.finished);
 assert(cp_rogue_key(&g,2,false)==CP_CMD_SOUND&&!g.rogue.muted);
 cp_rogue_key(&g,1,false);assert(g.rogue.cursor==5);cp_rogue_key(&g,1,false);assert(!g.rogue.cursor);
 cp_rogue_key(&g,0,false);assert(g.rogue.cursor==5);cp_rogue_key(&g,2,true);assert(!g.paused);
}
static void records(void){
 cp_records_t b,copy;cp_records_init(&b);assert(cp_records_valid(&b));
 for(unsigned k=0;k<15;++k){start(k+1);play();g.score=k*10;g.drops=k;grant(k%28,3);cp_rogue_finish(&g);assert(cp_records_add(&b,&g));assert(cp_records_valid(&b));}
 assert(b.rows[0].chips==140&&b.rows[9].chips==50&&cp_record_level(&b.rows[0],14)==3);
 memcpy(&copy,&b,sizeof(b));assert(cp_records_valid(&copy));copy.rows[0].chips++;assert(!cp_records_valid(&copy));
 start(50);play();g.rogue.practice=true;g.score=99999;cp_rogue_finish(&g);assert(!cp_records_add(&b,&g)&&b.rows[0].chips==140);
 start(51);assert(g.bank==CP_RUN_COINS&&g.score==0&&g.rogue.view==CP_VIEW_DRAFT); /* Persist records, never run state. */
 printf("records=%zu rogue=%zu bytes\n",sizeof(b),sizeof(g.rogue));
}
static void physical(void){
 /* Each effect is selected alone, then all 28 at tier three together. */
 for(unsigned id=0;id<=CP_RELIC_COUNT;++id){start(100+id);play();memset(g.rogue.relic,0,sizeof(g.rogue.relic));g.rogue.practice=true;
  if(id==CP_RELIC_COUNT)for(unsigned k=0;k<CP_RELIC_COUNT;++k)grant(k,3);else grant(id,3);
  g.bank=80;g.autoplay=true;
  for(unsigned frame=0;frame<1200;++frame){
   if(frame%180==0)cp_reward_event(&g,(cp_event_t)(1+(frame/180)%CP_EVENT_MAX));
   if(frame%90==0){cp_rogue_reel(&g);cp_rogue_event(&g);}
   tick(1);assert(g.overflow_steps==0&&cp_coin_count(&g)<=CP_MAX_COINS);
  }
 }
 for(unsigned seed=1;seed<=8;++seed){start(seed*1771);play();g.autoplay=true;
  for(unsigned k=0;k<3600&&!g.rogue.finished;++k)tick(1);
  assert(g.bank<=999&&g.score<=CP_CHIP_LIMIT&&g.drops>0);
  /* A productive build may sustain itself; voluntary settlement is always available. */
  cp_rogue_finish(&g);assert(g.rogue.finished&&g.paused);
  printf("run seed%u chips%lu paid%lu level%u seconds%.1f\n",seed,(unsigned long)g.score,(unsigned long)g.drops,g.rogue.level,(double)g.game_time);
 }

}
int main(void){sound_controls();economy();chip_fever();idle_budget();effects();records();physical();puts("28 relics / bounded synergies / run economy / controls / leaderboard CRC: PASS");}
