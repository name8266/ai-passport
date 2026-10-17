#include "pusher_physics.h"
#include "coin_save.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static cp_game_t g,restored,before;
static cp_save_t save;
static void run(float seconds){for(unsigned i=0;i<(unsigned)(seconds*CP_HZ+.5f);++i){cp_step(&g,CP_DT);assert(cp_valid(&g)&&!g.overflow_steps);}}
static void empty(unsigned seed){cp_init(&g,seed,120,0,1);memset(g.coins,0,sizeof(g.coins));}
int main(void){
    empty(8266);run(25);assert(g.events==0&&g.arcade.towers==0); /* Idle cabinet stays idle. */
    g.paused=true;cp_arcade_gate(&g,g.arcade.lane);assert(!g.arcade.spins);g.paused=false;
    cp_arcade_gate(&g,(g.arcade.lane+1)%3);assert(!g.arcade.spins);
    cp_arcade_gate(&g,g.arcade.lane);assert(g.arcade.spins==1&&g.arcade.spin_ticks==45&&g.drops==0);
    cp_arcade_gate(&g,g.arcade.lane);assert(g.arcade.spins==1);
    run(1);assert(!g.events);g.paused=true;unsigned left=g.arcade.spin_ticks;cp_step(&g,1);assert(g.arcade.spin_ticks==left);g.paused=false;
    run(.6f);assert(g.events&&g.drops==0);cp_arcade_gate(&g,g.arcade.lane);assert(g.arcade.spins==1);
    /* All eight ordinary events appear before the bag repeats; jackpots may interrupt. */
    unsigned seen=1u<<g.arcade.last_result;
    for(unsigned i=0;i<20&&__builtin_popcount(seen&~(1u<<CP_EVENT_JACKPOT))<8;++i){
        g.arcade.cooldown=0;cp_arcade_gate(&g,g.arcade.lane);run(1.6f);seen|=1u<<g.arcade.last_result;
    }
    assert(__builtin_popcount(seen&~(1u<<CP_EVENT_JACKPOT))==8);
    empty(9);g.autoplay=true;for(unsigned i=0;i<20*CP_HZ&&!g.events;++i)run(CP_DT);assert(g.arcade.spins>0&&g.events>0&&g.drops<24);
    /* Both side carriages deliver twelve real coins through an open door. */
    for(unsigned side=0;side<2;++side){empty(31+side);g.arcade.next_side=(uint8_t)side;g.arcade.tower_queue=1;
        run(CP_DT);assert(g.arcade.towers==1&&g.arcade.tower_coins==12&&cp_coin_count(&g)==12);
        float start_x=g.coins[0].p.x;assert(side?start_x==208:start_x==0);
        run(2.5f);assert(cp_tower_count(&g)>=2);run(1);unsigned inside=0;
        for(unsigned i=0;i<CP_MAX_COINS;++i)if(g.coins[i].active&&g.coins[i].p.x>20&&g.coins[i].p.x<188)++inside;
        printf("tower side%u inside%u tower%u contacts%u\n",side,inside,cp_tower_count(&g),g.peak_contacts);assert(inside>=10&&g.arcade.tower_ticks==0);
    }
    /* Multiplication creates physical gold copies, not a score-only award. */
    empty(123);for(unsigned k=0;k<6;++k)cp_spawn(&g,CP_COIN_GOLD,cp_v(35+27*k,110,1.3f),(cp_q4){1,0,0,0});
    cp_reward_event(&g,CP_EVENT_DOUBLE);run(2);assert(g.arcade.clones>=6&&g.drops==0&&g.arcade.double_ticks>0);
    printf("double clones%u bodies%u\n",g.arcade.clones,cp_coin_count(&g));
    /* A real hole removes support. Coins fall below the bed before collection. */
    empty(123);for(unsigned k=0;k<6;++k)cp_spawn(&g,CP_COIN_GOLD,cp_v(35+27*k,110,1.3f),(cp_q4){1,0,0,0});
    cp_reward_event(&g,CP_EVENT_BLACKHOLE);run(7);
    printf("siphon%u collected%u bodies%u contacts%u\n",g.arcade.siphoned,g.collected,cp_coin_count(&g),g.peak_contacts);
    assert(g.arcade.siphoned>=6&&g.collected==g.arcade.siphoned&&g.lost==0);
    empty(8);cp_spawn(&g,CP_COIN_GOLD,cp_v(104,CP_FRONT_EDGE+17,12),(cp_q4){1,0,0,0});cp_reward_event(&g,CP_EVENT_FEVER);run(2);
    assert(g.collected==1&&g.bank==122);
    cp_init(&g,12,120,0,1);cp_reward_event(&g,CP_EVENT_SHAKE);run(2);assert(g.arcade.shake_ticks>0&&cp_tower_count(&g)<36);
    cp_save_encode(&save,&g);assert(cp_save_decode(&restored,&save)&&memcmp(&g.arcade,&restored.arcade,sizeof(g.arcade))==0);
    save.arcade.tower_queue=255;const unsigned char *raw=(const unsigned char *)&save;
    save.checksum=2166136261u;for(unsigned k=sizeof(save.checksum);k<sizeof(save);++k)save.checksum=(save.checksum^raw[k])*16777619u;
    before=restored;assert(!cp_save_decode(&restored,&save)&&memcmp(&before,&restored,sizeof(before))==0);
    for(unsigned seed=1;seed<=4;++seed){cp_init(&g,seed,1000,0,1);g.autoplay=true;run(240);
        printf("arcade seed%u spins%u events%u towers%u siphon%u clones%u waits%u contacts%u\n",seed,g.arcade.spins,g.events,g.arcade.towers,g.arcade.siphoned,g.arcade.clones,g.capacity_waits,g.peak_contacts);
        assert(g.arcade.spins>=5&&g.arcade.towers>=2&&g.capacity_waits==0);
    }
    puts("Arcade reels, side towers, physical siphon/clones and repeated play: PASS");
}
