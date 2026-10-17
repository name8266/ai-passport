#include "pusher_physics.h"
#include "coin_save.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static cp_game_t g,restored;
static cp_save_t save;
static void resign(cp_save_t *s){const uint8_t *p=(const uint8_t *)s;uint32_t h=2166136261u;for(unsigned i=sizeof(s->checksum);i<sizeof(*s);++i)h=(h^p[i])*16777619u;s->checksum=h;}
static void mechanics_init(cp_game_t *game,uint32_t seed,uint32_t bank,uint32_t best,uint32_t round){
    cp_init(game,seed,bank,best,round);game->features=false;
}
static void run(float seconds) {for(unsigned i=0;i<(unsigned)(seconds*CP_HZ);++i)cp_step(&g,CP_DT);}
static void empty(void) {mechanics_init(&g,123,200,0,1);memset(g.coins,0,sizeof(g.coins));}
int main(void)
{
    const int32_t numbers[]={0,1,-1,32768,-32768,65535,16777217,2147483647,(-2147483647-1)};
    for(unsigned i=0;i<sizeof(numbers)/sizeof(numbers[0]);++i)for(unsigned fraction=0;fraction<=16;fraction+=4){float expected=(float)numbers[i]/(float)(1u<<fraction),actual=cp_fixed_float(numbers[i],fraction);assert(memcmp(&expected,&actual,sizeof(actual))==0);}
    /* No input or pressure: two supported towers must remain constructed. */
    mechanics_init(&g,123,200,0,1);unsigned towers=cp_tower_count(&g);
    assert(towers==18);run(30);assert(cp_tower_count(&g)==towers&&g.prizes==0&&g.lost==0&&g.collected==0);
    assert(g.overflow_steps==0&&cp_valid(&g));
    /* Impact on the lowest layer wakes the supported rigid bodies above it. */
    cp_coin_t *hit=cp_spawn(&g,CP_COIN_COPPER,cp_v(63,103,7),(cp_q4){.98f,.15f,0,0});
    assert(hit);hit->v=cp_v(0,140,0);
    unsigned tilted=0;
    for(unsigned step=0;step<5*CP_HZ;++step){
        cp_step(&g,CP_DT);unsigned n=0;
        for(unsigned i=0;i<CP_MAX_COINS;++i)if(g.coins[i].active&&fabsf(cp_body_axis(&g.coins[i],2).z)<.9f)++n;
        if(n>tilted)tilted=n;
    }
    assert(cp_tower_count(&g)<towers&&tilted>2&&cp_valid(&g));
    /* A partially overhanging disk with its center inside stays supported. */
    empty();cp_coin_t *edge_coin=cp_spawn(&g,CP_COIN_COPPER,cp_v(104,CP_FRONT_EDGE-1,1.3f),(cp_q4){1,0,0,0});run(6);assert(edge_coin->active&&g.collected==0&&edge_coin->p.z>.8f);
    /* Gravity, finite ledge support and angular tipping, not a centerline deletion. */
    empty();cp_coin_t *c=cp_spawn(&g,CP_COIN_COPPER,cp_v(104,CP_FRONT_EDGE+4,1.3f),(cp_q4){1,0,0,0});
    assert(c);run(.5f);assert(fabsf(c->q.x)>.02f||fabsf(c->q.y)>.02f);run(3);
    assert(g.collected==1&&g.bank==201&&!c->active);
    empty();cp_spawn(&g,CP_COIN_COPPER,cp_v(15,CP_SIDE_EDGE+20,1.3f),(cp_q4){1,0,0,0});run(4);
    assert(g.lost==1&&g.collected==0);
    empty();cp_spawn(&g,CP_PRIZE_RED,cp_v(104,CP_FRONT_EDGE+17,12),(cp_q4){1,0,0,0});run(2);
    assert(g.prizes==1&&g.score==40&&g.bank==240);
    /* Tilted landing becomes a supported disk with a normalized orientation. */
    empty();c=cp_spawn(&g,CP_COIN_COPPER,cp_v(104,130,35),(cp_q4){.70710678f,.70710678f,0,0});
    c->w=cp_v(2,1,0);run(6);assert(c->active&&c->p.z<10&&c->p.z>0&&cp_valid(&g));
    /* Swept thin caps: flat coins cannot tunnel through a disk or the floor. */
    empty();cp_coin_t *base=cp_spawn(&g,CP_COIN_COPPER,cp_v(104,130,1.3f),(cp_q4){1,0,0,0});
    c=cp_spawn(&g,CP_COIN_COPPER,cp_v(104,130,35),(cp_q4){1,0,0,0});run(6);
    assert(base->active&&c->active&&c->p.z-base->p.z>2.4f&&g.lost==0);
    empty();c=cp_spawn(&g,CP_COIN_COPPER,cp_v(104,130,35),(cp_q4){1,0,0,0});c->v.z=-200;run(3);
    assert(c->active&&c->p.z>.8f&&g.lost==0);
    /* The moving pusher must translate a three-layer lower bed, including
     * its bottom row, rather than only making a taller heap. */
    empty();
    for(unsigned layer=0;layer<3;++layer)for(unsigned row=0;row<4;++row)
        cp_spawn(&g,CP_COIN_COPPER,cp_v(104,90+16*row,1.3f+layer*2.6f),(cp_q4){1,0,0,0});
    run(1.4f);
    for(unsigned layer=0;layer<3;++layer)for(unsigned row=0;row<3;++row){
        c=&g.coins[layer*4+row];assert(c->active&&c->p.y>94+16*row);
        assert(c->p.z>.7f+layer*2.3f&&c->p.z<2+layer*3.0f);
    }
    assert(cp_valid(&g)&&g.overflow_steps==0);
    /* Every insertion has the same inlet. Swing phase, then actual capsule
     * contacts, must distribute single coins without an assigned exit lane. */
    float xmin=1000,xmax=-1000;unsigned peg_total=0;
    for(unsigned phase=0;phase<16;++phase){
        empty();g.game_time=phase*.15f;assert(cp_drop(&g));c=&g.coins[0];
        assert(c->feeder==1&&c->p.x==CP_ENTRY_X&&c->p.y==CP_ENTRY_Y&&c->p.z==CP_ENTRY_Z);
        float exit_x=0;unsigned exit_step=0;
        for(unsigned step=1;step<=8*CP_HZ;++step){cp_step(&g,CP_DT);
            if(g.feeder_exits&&!exit_step){exit_x=c->p.x;exit_step=step;}}
        assert(exit_step>0&&g.feeder_exits==1&&c->feeder==0&&g.rod_hits>=1);
        peg_total+=g.peg_hits;
        if(exit_x<xmin)xmin=exit_x;if(exit_x>xmax)xmax=exit_x;
        assert(cp_valid(&g)&&g.overflow_steps==0);
    }
    assert(xmax-xmin>50&&peg_total>=8);
    /* Casual insertion at three swing phases, 20 paid coins total, keeping
     * the existing requirement for visible returns and no capacity jam. */
    for(unsigned timing=0;timing<3;++timing)for(unsigned seed=1;seed<=4;++seed){
        mechanics_init(&g,seed,120,0,1);run(timing*.4f);unsigned first=0,full=0;
        for(unsigned t=0;t<45*CP_HZ;++t){
            if(t%39==0&&g.drops<20)(void)cp_drop(&g);
            cp_step(&g,CP_DT);if(g.collected&&!first)first=t;
            if(cp_coin_count(&g)==CP_MAX_COINS)++full;
            assert(cp_valid(&g)&&g.overflow_steps==0);
        }
        printf("casual phase%u seed%u paid%u return%u prizes%u exits%u first%.1f\n",timing,seed,g.drops,g.collected,g.prizes,g.feeder_exits,first/(float)CP_HZ);
        assert(g.drops==20&&g.collected>=8&&first>0&&first<14*CP_HZ&&full==0);
        assert(g.feeder_exits==20&&g.rod_hits>0&&g.peg_hits>0);
    }
    /* Rendering at 12.5 Hz must still advance 30 physics steps per real second. */
    mechanics_init(&g,1,120,0,1);for(unsigned i=0;i<125;++i)cp_step(&g,.08f);
    assert(g.steps==10*CP_HZ&&fabsf(g.game_time-10)<.002f);
    /* Full capacity never consumes a paid credit or removes another body. */
    while(cp_spawn(&g,CP_COIN_COPPER,cp_v(104,130,30),(cp_q4){1,0,0,0})) {}
    uint32_t bank=g.bank;assert(!cp_drop(&g)&&g.bank==bank);
    g.paused=true;float t=g.game_time;cp_step(&g,.1f);assert(t==g.game_time);
    /* Persist rotations and angular velocities; reject malformed snapshots. */
    mechanics_init(&g,99,120,7,2);assert(cp_drop(&g));run(1);
    cp_save_encode(&save,&g);assert(cp_save_decode(&restored,&save));
    assert(restored.bank==g.bank&&cp_coin_count(&restored)==cp_coin_count(&g));
    assert(restored.rod_hits==g.rod_hits&&restored.peg_hits==g.peg_hits);
    for(unsigned i=0;i<CP_MAX_COINS;++i)if(g.coins[i].active){
        assert(g.coins[i].feeder==restored.coins[i].feeder&&g.coins[i].feeder_mask==restored.coins[i].feeder_mask);
        assert(fabsf(g.coins[i].p.z-restored.coins[i].p.z)<.04f);
        assert(fabsf(g.coins[i].q.w-restored.coins[i].q.w)<.001f);
    }
    /* CP_CHIP is a valid body kind in legacy snapshot fixtures. */
    unsigned chip_index=3;assert(g.coins[chip_index].active);
    uint8_t old_kind=g.coins[chip_index].kind;
    g.coins[chip_index].kind=CP_CHIP;cp_body_refresh(&g.coins[chip_index]);
    cp_save_encode(&save,&g);assert(cp_save_decode(&restored,&save));
    assert(restored.coins[chip_index].kind==CP_CHIP);
    g.coins[chip_index].kind=old_kind;cp_body_refresh(&g.coins[chip_index]);
    cp_save_encode(&save,&g);
    save.coins[3].q[0]^=1;assert(!cp_save_decode(&restored,&save));
    cp_save_encode(&save,&g);cp_game_t unchanged=restored;
    save.coins[3].active=2;resign(&save);assert(!cp_save_decode(&restored,&save));assert(memcmp(&unchanged,&restored,sizeof(restored))==0);
    cp_save_encode(&save,&g);memset(save.coins[3].q,0,sizeof(save.coins[3].q));resign(&save);assert(!cp_save_decode(&restored,&save));assert(memcmp(&unchanged,&restored,sizeof(restored))==0);

    /* Long dense piles, repeated injection, fixed inlet and tower collapses. */
    for(unsigned seed=1;seed<=4;++seed) {
        mechanics_init(&g,seed,240,0,1);
        for(unsigned frame=0;frame<60*CP_HZ;++frame) {
            if(frame%(CP_HZ*3/5)==0){(void)cp_drop(&g);}
            cp_step(&g,CP_DT);assert(cp_valid(&g));assert(g.overflow_steps==0);
        }
        assert(cp_coin_count(&g)<=CP_MAX_COINS);assert(g.prizes>=1&&g.collected>10);
        printf("seed %u: paid %lu payout %lu lost %lu prizes %lu contacts %u\n",seed,
            (unsigned long)g.drops,(unsigned long)g.collected,(unsigned long)g.lost,(unsigned long)g.prizes,g.peak_contacts);
    }
    cp_save_encode(&save,&g);unchanged=restored;save.coins[3].active=13;resign(&save);
    assert(!cp_save_decode(&restored,&save)&&memcmp(&unchanged,&restored,sizeof(restored))==0);
    mechanics_init(&g,9,0,5,1);assert(!cp_next_round(&g));
    for(unsigned i=0;i<CP_MAX_COINS;++i)if(g.coins[i].kind>=CP_PRIZE_RED)g.coins[i].active=false;
    g.score=5;assert(cp_next_round(&g)&&g.bank==30&&g.score==5&&g.round==2&&cp_prize_count(&g)==3);
    /* Gameplay is tested independently from the unmodified mechanical fixtures. */
    cp_init(&g,8266,120,0,1);
    for(unsigned i=0;i<CP_MAX_COINS;++i)if(g.coins[i].active&&g.coins[i].kind<CP_PRIZE_RED)assert(g.coins[i].kind==CP_COIN_GOLD);
    memset(g.coins,0,sizeof(g.coins));g.bank=0;cp_reward_event(&g,CP_EVENT_RAIN);run(14);
    assert(g.drops==0&&g.bonus_drops>=8);
    assert(g.feeder_exits>=4&&cp_valid(&g));
    /* Special prize effects are activated by real front collection. */
    for(unsigned kind=0;kind<3;++kind){
        cp_init(&g,100+kind,120,0,1);memset(g.coins,0,sizeof(g.coins));
        cp_spawn(&g,(cp_coin_kind_t)(CP_PRIZE_RED+kind),cp_v(104,CP_FRONT_EDGE+17,12),(cp_q4){1,0,0,0});run(2);
        assert(g.prizes==1&&g.event_kind==1+kind);
        if(kind==0)assert(g.rush_ticks>0);
        if(kind==1)assert(g.bonus_drops>0||g.bonus_queue>0);
        if(kind==2)assert(g.guard_ticks>0);
        run(8);assert(cp_prize_count(&g)>0&&g.restocks>=3&&cp_valid(&g));
    }
    /* Guard covers retain a side coin, and expiry removes its support. */
    cp_init(&g,99,120,0,1);memset(g.coins,0,sizeof(g.coins));cp_reward_event(&g,CP_EVENT_SAVE);
    c=cp_spawn(&g,CP_COIN_GOLD,cp_v(15,CP_SIDE_EDGE+20,1.3f),(cp_q4){1,0,0,0});run(4);
    assert(c->active&&g.lost==0);g.guard_ticks=1;run(CP_DT);assert(g.guard_ticks==0);
    c->p=cp_v(15,CP_SIDE_EDGE+20,1.3f);c->v=cp_v(0,0,0);c->sleeping=false;run(4);
    assert(g.lost>=1&&(!c->active||c->kind!=CP_COIN_GOLD));
    /* Arcade draws depend on a lit physical exit, never a credit threshold. */
    cp_init(&g,5566,120,0,1);cp_arcade_gate(&g,g.arcade.lane);assert(g.arcade.spin_ticks==45&&g.drops==0);
    run(2);assert(g.arcade.spins==1&&g.events>0&&g.drops==0);
    cp_reward_event(&g,CP_EVENT_JACKPOT);
    cp_save_encode(&save,&g);assert(cp_save_decode(&restored,&save));
    assert(restored.bonus_queue==g.bonus_queue&&restored.rush_ticks==g.rush_ticks&&restored.guard_ticks==g.guard_ticks&&restored.jackpots==g.jackpots);
    unchanged=restored;save.bonus_queue=65535;resign(&save);assert(!cp_save_decode(&restored,&save)&&memcmp(&unchanged,&restored,sizeof(restored))==0);
    /* A simultaneous secondary award must not erase the jackpot celebration. */
    cp_init(&g,8266,120,0,1);cp_reward_event(&g,CP_EVENT_JACKPOT);
    g.event_ticks=3*CP_HZ;cp_reward_event(&g,CP_EVENT_RAIN);
    assert(g.event_kind==CP_EVENT_JACKPOT&&g.event_ticks==3*CP_HZ&&g.bonus_queue==20);
    cp_reward_event(&g,CP_EVENT_SAVE);assert(g.event_kind==CP_EVENT_JACKPOT&&g.guard_ticks==15*CP_HZ);
    g.event_ticks=0;cp_reward_event(&g,CP_EVENT_RUSH);assert(g.event_kind==CP_EVENT_RUSH&&g.event_ticks==4*CP_HZ);
    /* Refills and bonus queues must remain bounded through several prize cycles. */
    for(unsigned seed=1;seed<=4;++seed){cp_init(&g,seed,240,0,1);g.autoplay=true;
        run(120);assert(cp_valid(&g)&&!g.overflow_steps&&g.events>0&&g.restocks>0&&g.bonus_drops>0&&g.capacity_waits==0&&g.circulation_cycles>0);
        for(unsigned i=0;i<CP_MAX_COINS;++i)if(g.coins[i].active&&g.coins[i].kind<CP_PRIZE_RED)assert(g.coins[i].kind==CP_COIN_GOLD);
        printf("arcade seed%u paid%u bonus%u prizes%u restock%u jackpot%u contacts%u\n",seed,g.drops,g.bonus_drops,g.prizes,g.restocks,g.jackpots,g.peak_contacts);
        printf("flow seed%u cycles%u capacity_waits%u bodies%u\n",seed,g.circulation_cycles,g.capacity_waits,cp_coin_count(&g));
    }
    /* Reproduce a large rain backlog without deleting bodies or waiting for
     * the physical pool: circulation must outpace sustained incoming coins. */
    cp_init(&g,8266,120,0,1);g.bonus_queue=128;
    run(300);
    assert(cp_valid(&g)&&!g.overflow_steps&&g.capacity_waits==0&&g.bonus_drops>=128&&g.feeder_exits>=120&&g.circulation_cycles>0);
    printf("rain128 bonus%u exits%u circulation%u capacity_waits%u contacts%u\n",g.bonus_drops,g.feeder_exits,g.circulation_cycles,g.capacity_waits,g.peak_contacts);
    puts("coin pusher 3D: PASS (tower support/impact, tipping, side loss, prizes, time, save, dense stress)");
}
