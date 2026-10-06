#include "plinko_core.h"
#include "tiny3d_math.h"
static uint32_t random_next(plinko_ball_t *b) {
    uint32_t x=b->rng; x^=x<<13; x^=x>>17; x^=x<<5;
    return b->rng=x;
}
int plinko_peg_count(int row) { return (row&1)?8:9; }
void plinko_peg_position(int row,int col,int *x,int *y) {
    *x=24+(row&1)*10+col*20; *y=20+row*16;
}
void plinko_ball_init(plinko_ball_t *b,uint32_t seed,uint8_t speed) {
    *b=(plinko_ball_t){.rng=seed?seed:1,.speed=speed>2?1:speed};
    b->x=(100+(int)(random_next(b)%9))*256; b->y=5*256;
    b->vx=(int)(random_next(b)%81)-40; b->vy=120;
}
uint16_t plinko_ball_step(plinko_ball_t *b) {
    if(!b || b->done) return 0;
    uint16_t hit=0;
    /* Two substeps avoid tunnelling through the small pegs. Velocities use pixels/60Hz tick. */
    for(int sub=0;sub<2;sub++) {
        b->vy+=b->speed==0?17:b->speed==2?30:23;
        if(b->vy>1000) b->vy=1000;
        b->x+=b->vx/2; b->y+=b->vy/2;
        for(int row=0;row<PLINKO_CORE_ROWS;row++) {
            for(int col=0;col<plinko_peg_count(row);col++) {
                int px,py; plinko_peg_position(row,col,&px,&py);
                int32_t dx=b->x-px*256,dy=b->y-py*256;
                if(dx>2048||dx< -2048||dy>2048||dy< -2048) continue;
                uint32_t distance=t3_isqrt_u64((int64_t)dx*dx+(int64_t)dy*dy);
                if(distance>=2048) continue;
                if(!distance) { dx=(random_next(b)&1)?128:-128;dy=-2048;distance=2052; }
                int32_t nx=dx*1024/(int32_t)distance,ny=dy*1024/(int32_t)distance;
                int32_t inward=(b->vx*nx+b->vy*ny)/1024;
                b->x=px*256+nx*2056/1024;
                b->y=py*256+ny*2056/1024;
                if(inward<0) {
                    int32_t impulse=-inward*365/256;
                    b->vx=(b->vx+impulse*nx/1024)*240/256;
                    b->vy=(b->vy+impulse*ny/1024)*240/256;
                    /* Break a perfectly centred unstable balance, never a cumulative sideways kick. */
                    if(dx<160&&dx> -160&&dy<0) b->vx+=(random_next(b)&1)?90:-90;
                    ++b->collisions; hit++;
                }
            }
        }
        if(b->x<8*256) { b->x=8*256;b->vx=-b->vx*170/256; }
        if(b->x>200*256) { b->x=200*256;b->vx=-b->vx*170/256; }
        if(b->y<4*256) { b->y=4*256;if(b->vy<0)b->vy=-b->vy/2; }
        if(b->vx>700)b->vx=700;
        if(b->vx< -700)b->vx=-700;
        if(b->y>=145*256) {
            b->y=145*256; int bin=(b->x/256-14)/20;
            b->bin=(uint8_t)(bin<0?0:bin>8?8:bin);b->done=true;break;
        }
    }
    ++b->ticks;return hit;
}

uint8_t plinko_balls_init(plinko_ball_t *balls,uint8_t count,uint32_t seed,uint8_t speed) {
    if(!balls)return 0;
    if(count<1)count=1;
    if(count>PLINKO_MAX_BALLS)count=PLINKO_MAX_BALLS;
    for(unsigned i=0;i<count;i++) {
        plinko_ball_init(&balls[i],seed+i*2654435761u,speed);
        /* All balls start on the same tick, spread across the central release gate. */
        if(count>1)balls[i].x=(104*256)+((int)(2*i)-(count-1))*6*256;
    }
    return count;
}
