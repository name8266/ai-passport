#include "plinko_core.h"
#include <assert.h>
#include <stdio.h>
int main(void) {
    for(int speed=0;speed<3;speed++) {
        unsigned bins[9]={0},max_ticks=0,total=0;
        for(uint32_t seed=1;seed<=1000;seed++) {
            plinko_ball_t b;plinko_ball_init(&b,seed*2654435761u,(uint8_t)speed);
            for(int step=0;step<600&&!b.done;step++) {
                plinko_ball_step(&b);
                assert(b.x>=8*256&&b.x<=200*256);
            }
            assert(b.done);assert(b.bin<9);assert(b.collisions>0);
            ++bins[b.bin];total+=b.ticks;if(b.ticks>max_ticks)max_ticks=b.ticks;
        }
        unsigned interior=0,occupied=0;
        for(int i=0;i<9;i++){if(bins[i])occupied++;if(i>0&&i<8)interior+=bins[i];}
        printf("plinko speed=%d bins=",speed);for(int i=0;i<9;i++)printf("%u,",bins[i]);
        printf(" mean_ms=%u max_ms=%u\n",total/60,max_ticks*1000/60);
        assert(occupied>=7);assert(interior>800);assert(max_ticks<360);
    }
    assert(plinko_balls_init(NULL,10,1,1)==0);
    plinko_ball_t limited[PLINKO_MAX_BALLS];
    assert(plinko_balls_init(limited,0,1,1)==1);
    assert(plinko_balls_init(limited,255,1,1)==10);
    for(uint8_t count=1;count<=PLINKO_MAX_BALLS;count++)for(uint8_t speed=0;speed<3;speed++) {
        for(uint32_t seed=1;seed<=100;seed++) {
            plinko_ball_t balls[PLINKO_MAX_BALLS],copy[PLINKO_MAX_BALLS];
            assert(plinko_balls_init(balls,count,seed,speed)==count);
            plinko_balls_init(copy,count,seed,speed);
            for(unsigned i=0;i<count;i++) {
                assert(balls[i].ticks==0&&!balls[i].done&&balls[i].y==5*256);
                assert(balls[i].x==copy[i].x&&balls[i].rng==copy[i].rng);
                if(i)assert(balls[i].x-balls[i-1].x==12*256);
            }
            for(unsigned tick=0;tick<600;tick++)for(unsigned i=0;i<count;i++)plinko_ball_step(&balls[i]);
            for(unsigned i=0;i<count;i++)assert(balls[i].done&&balls[i].bin<9&&balls[i].ticks<600);
        }
    }
    puts("plinko simultaneous 1-10 balls at all speeds: PASS");
    return 0;
}
