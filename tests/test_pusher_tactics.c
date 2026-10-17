#include "pusher_physics.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static cp_game_t g;
static void start(unsigned seed){cp_init(&g,seed,30,0,1);cp_rogue_start(&g,0);cp_rogue_choose(&g,0);memset(g.coins,0,sizeof(g.coins));g.features=false;g.rogue.pending=0;}
static cp_coin_t *coin(unsigned kind,float x,float y){cp_coin_t *b=cp_spawn(&g,kind==CP_SP_METEOR?CP_CHIP:CP_COIN_GOLD,cp_v(x,y,1.3f),(cp_q4){1,0,0,0});assert(b);b->special=(uint8_t)kind;return b;}
static void tick(unsigned n){for(unsigned k=0;k<n;++k){if(g.rogue.view==CP_VIEW_DRAFT)cp_rogue_choose(&g,k%3);if(g.rogue.view==CP_VIEW_SUPPLY)cp_tactics_supply(&g,k%3);cp_step(&g,CP_DT);assert(cp_valid(&g)&&!g.overflow_steps);}}
static void stock_and_controls(void){
 start(123);for(unsigned kind=1;kind<=8;++kind)cp_tactics_grant(&g,kind,100);for(unsigned k=0;k<8;++k)assert(g.tactics.stock[k]==4);
 assert(g.tactics.queue_count==CP_SPECIAL_QUEUE&&cp_tactics_valid(&g.tactics));
 unsigned bank=g.bank,head=cp_tactics_auto_kind(&g);assert(head==CP_SP_SEED);
 assert(cp_drop(&g));assert(g.bank==bank&&g.tactics.stock[head-1]==3&&g.tactics.casts==1);
 assert(g.coins[0].p.x==CP_ENTRY_X&&g.coins[0].feeder==1&&g.coins[0].special==head);
 unsigned count=g.tactics.queue_count;assert(!cp_drop(&g)&&g.tactics.queue_count==count&&g.bank==bank);
 assert(cp_rogue_key(&g,1,false)==CP_CMD_NONE&&g.rogue.view==CP_VIEW_TRAY&&g.paused);
 unsigned step=g.steps;cp_step(&g,2);assert(g.steps==step);
 cp_rogue_key(&g,1,false);assert(g.tactics.selected==1);
 assert(cp_rogue_key(&g,2,false)==CP_CMD_NONE&&g.rogue.view==CP_VIEW_PLAY&&g.tactics.queue_count==count);
 /* Inspector cursor cannot reorder the FIFO. Every normal button/burst uses the same head. */
 assert(cp_tactics_auto_kind(&g)==CP_SP_WATER);assert(!cp_tactics_cast(&g,CP_SP_BLAST));
 while(g.tactics.queue_count){memset(g.coins,0,sizeof(g.coins));unsigned id=cp_tactics_auto_kind(&g);assert(cp_drop(&g)&&g.coins[0].special==id&&g.bank==bank);assert(cp_tactics_valid(&g.tactics));}
 memset(g.coins,0,sizeof(g.coins));assert(cp_drop(&g)&&g.bank==bank-1&&g.drops==1);
 cp_tactics_grant(&g,CP_SP_BLAST,1);unsigned casts=g.tactics.casts;tick(90);assert(g.tactics.casts==casts&&g.tactics.queue_count==1); /* No hidden cast timer. */
 assert(cp_rogue_key(&g,1,true)==CP_CMD_BURST);
}
static void fair_autoplay(void){
 start(1988);for(unsigned id=1;id<=8;++id)cp_tactics_grant(&g,id,1);
 unsigned seen=0;
 for(unsigned k=0;k<40;++k){unsigned id=cp_tactics_auto_kind(&g);if(!id)break;seen|=1u<<(id-1);assert(cp_tactics_cast(&g,id));memset(g.coins,0,sizeof(g.coins));}
 assert(seen==255);for(unsigned k=0;k<8;++k)assert(g.tactics.used[k]>0);
}
static void contact_links(void){
 start(5);cp_coin_t *seed=coin(CP_SP_SEED,100,130);coin(CP_SP_WATER,114,130);tick(1);
 assert(seed->trait&CP_TRAIT_WATERED);assert(g.tactics.links==1&&g.tactics.grown==6);
 tick(15);assert(g.tactics.grown==6); /* A seed grows once. */
 start(6);seed=coin(CP_SP_SEED,70,130);coin(CP_SP_WATER,130,130);tick(1);assert(!(seed->trait&CP_TRAIT_WATERED)&&!g.tactics.links);
 start(7);cp_coin_t *blast=coin(CP_SP_BLAST,100,130);coin(CP_SP_PRISM,114,130);tick(1);
 assert((blast->trait&(CP_TRAIT_CHARGED|CP_TRAIT_DOUBLE))==(CP_TRAIT_CHARGED|CP_TRAIT_DOUBLE));tick(35);assert(g.tactics.blasts==1&&(blast->trait&CP_TRAIT_FIRED));
 start(8);coin(CP_SP_METEOR,100,130);coin(CP_SP_MAGNET,114,130);tick(1);assert(g.tactics.links==1&&g.tactics.blasts==1);
 start(9);coin(CP_SP_PRISM,100,130);cp_coin_t *gold=coin(CP_SP_NONE,114,130);tick(1);assert(gold->trait&CP_TRAIT_DOUBLE);
 memset(g.coins,0,sizeof(g.coins));g.rogue.relic[CP_R_COMBO]=0;gold=cp_spawn(&g,CP_COIN_GOLD,cp_v(104,150,-27),(cp_q4){1,0,0,0});gold->trait=CP_TRAIT_DOUBLE;unsigned score=g.score;tick(1);assert(g.score==score+2);
 start(10);coin(CP_SP_GUARD,104,130);g.features=true;tick(1);assert(g.guard_ticks>0);
 start(11);coin(CP_SP_ECHO,104,130);tick(1);assert(g.bonus_queue==3);tick(30);assert(g.bonus_queue==3);
 start(12);coin(CP_SP_MAGNET,104,130);gold=coin(CP_SP_NONE,130,130);tick(1);float vx=gold->v.x;tick(2);assert(gold->v.x<vx);
}
static void far_fields(void){
 start(20278729);coin(CP_SP_MAGNET,18,130);cp_coin_t *far=coin(CP_SP_NONE,190,130);far->p.z=120;tick(4);assert(g.tactics.links==0);
 start(20278730);coin(CP_SP_BLAST,40,130);coin(CP_SP_NONE,180,130);tick(35);assert(g.tactics.blasts==1);
 start(20278731);coin(CP_SP_WATER,18,130);coin(CP_SP_SEED,190,130);tick(4);assert(!g.tactics.links);
}
static void stages(void){
 start(44);
 for(unsigned act=0;act<CP_ACT_COUNT-1;++act){
  unsigned gross=g.tactics.stage_gross+cp_tactics_goal(&g);g.rogue.gross=gross;
  cp_tactics_tick(&g);assert(g.tactics.stage==act); /* Chip income alone cannot skip the physical quota. */
  g.collected=g.tactics.stage_collected+cp_tactics_quota(&g);cp_tactics_tick(&g);
  assert(g.rogue.view==CP_VIEW_SUPPLY&&g.tactics.stage==act+1&&g.paused&&g.tactics.stage_gross==gross);
  unsigned bank=g.bank;cp_tactics_supply(&g,act%3);assert(g.bank==bank+(act%3==0?12:act%3==1?7:18)&&g.rogue.view==CP_VIEW_PLAY);
 }
 g.rogue.gross=g.tactics.stage_gross+cp_tactics_goal(&g);g.collected=g.tactics.stage_collected+cp_tactics_quota(&g);cp_tactics_tick(&g);assert(!g.rogue.finished);
 g.features=true;cp_coin_t *c=cp_spawn(&g,CP_PRIZE_RED,cp_v(104,150,-27),(cp_q4){1,0,0,0});c->trait=CP_TRAIT_CROWN;tick(1);
 while(g.rogue.view==CP_VIEW_DRAFT)cp_rogue_choose(&g,0);cp_tactics_tick(&g);assert(g.tactics.won&&g.rogue.finished&&g.jackpots>0);
 start(45);g.tactics.stage_ticks=180*CP_HZ-1;tick(1);assert(g.rogue.finished&&g.tactics.end_reason==2&&!g.tactics.won);
 start(46);g.tactics.stage=CP_ACT_COUNT-1;g.tactics.crown_spawned=true;cp_coin_t *crown=cp_spawn(&g,CP_PRIZE_RED,cp_v(5,150,-27),(cp_q4){1,0,0,0});crown->trait=CP_TRAIT_CROWN;tick(1);assert(!g.tactics.crown_spawned&&!g.tactics.crown_collected&&g.tactics.crown_retry>g.steps);
 start(47);g.features=true;g.tactics.stage=CP_ACT_COUNT-1;g.arcade.tower_queue=2;tick(1);unsigned n=0;for(unsigned i=0;i<CP_MAX_COINS;++i)n+=g.coins[i].active&&(g.coins[i].trait&CP_TRAIT_CROWN);assert(n==1&&g.tactics.crown_spawned);
 cp_init(&g,48,40,0,1);cp_rogue_start(&g,0);assert(cp_coin_count(&g)==256);
 /* Dense stock must stay on its supports before the player inserts a coin. */
 g.rogue.pending=0;cp_rogue_view(&g,CP_VIEW_PLAY);
 for(unsigned k=0;k<30*CP_HZ;++k){cp_step(&g,CP_DT);assert(cp_valid(&g)&&!g.overflow_steps);}
 assert(cp_coin_count(&g)==256&&g.collected==0&&g.lost==0&&g.prizes==0);
}
int main(void){stock_and_controls();fair_autoplay();contact_links();far_fields();stages();puts("FIFO special queue / busy refund / no manual casting / five acts / dense cabinet / physical links: PASS");}
