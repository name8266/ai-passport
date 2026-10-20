#include "brake_app.h"
#include <string.h>
static void page(bh_app_t *a, bh_page_t p, int64_t now) {
    a->view.page=p;a->page_since=now;a->view.selection=0;a->view.animation=0;
    a->input_after=now+250000;
}
void bh_app_init(bh_app_t *a, int64_t now) {
    memset(a,0,sizeof(*a));a->view.battery=-1;a->view.rank=-1;
    bh_scores_clear(&a->view.scores);bh_reset(&a->view.car,0);
    bh_clock_reset(&a->clock,now);page(a,BH_HOME,now);
}
static void start(bh_app_t *a,int64_t now) {
    ++a->session;
    bh_reset(&a->view.car,a->session%4);
    a->view.rank=-1;a->view.saved=false;
    bh_clock_reset(&a->clock,now);page(a,BH_COUNTDOWN,now);a->view.countdown=3;
}
uint32_t bh_app_tick(bh_app_t *a,int64_t now) {
    uint32_t ev=0;
    a->view.animation=(unsigned)((now-a->page_since)/33333);
    if(a->view.page==BH_COUNTDOWN) {
        int64_t age=now-a->page_since;
        a->view.countdown=age<3000000?3-(int)(age/1000000):0;
        if(age>=3350000 && !a->held_mask) {
            page(a,BH_DRIVE,now);bh_clock_reset(&a->clock,now);ev|=BH_EV_START;
        }
    } else if(a->view.page==BH_DRIVE) {
        ev|=bh_advance(&a->view.car,&a->clock,now);
        if(a->view.car.end!=BH_RUNNING) {
            a->view.rank=bh_scores_add(&a->view.scores,&a->view.car);
            a->finished_session=a->session;
            page(a,a->view.car.end==BH_STOPPED?BH_RESULT:BH_CRASH,now);
            a->input_after=now+900000;
        }
    } else if(a->view.page==BH_CRASH && now-a->page_since>=2600000) {
        page(a,BH_RESULT,now);a->input_after=now+350000;
    }
    return ev;
}
uint32_t bh_app_input(bh_app_t *a,bh_key_t key,bool pressed,int64_t now) {
    uint32_t ev=bh_app_tick(a,now);
    if(key<BH_TENT||key>BH_THROTTLE)return ev;
    uint32_t bit=1u<<(unsigned)key;
    bool edge=pressed && !(a->held_mask&bit);
    if(pressed)a->held_mask|=bit;else a->held_mask&=~bit;
    bh_clock_key(&a->clock,bh_key_from_mask(a->held_mask));
    if(!edge||now<a->input_after)return ev;
    bh_view_t *v=&a->view;
    if(v->page==BH_HOME) {
        if(key==BH_TENT)v->selection=(v->selection+2)%3;
        else if(key==BH_BRAKE)v->selection=(v->selection+1)%3;
        else if(v->selection==0)start(a,now);
        else if(v->selection==1)page(a,BH_BOARD,now);
        else page(a,BH_HELP,now);
    } else if(v->page==BH_RESULT) {
        if(key==BH_THROTTLE) { if(v->selection==0)start(a,now);else page(a,BH_HOME,now); }
        else v->selection=1-v->selection;
    } else if(v->page==BH_BOARD) {
        if(key==BH_THROTTLE)page(a,BH_HOME,now);
        else v->board_page=1-v->board_page;
    } else if(v->page==BH_HELP && key==BH_THROTTLE)page(a,BH_HOME,now);
    return ev;
}
uint32_t bh_app_sync(bh_app_t *a,uint32_t mask,int64_t now) {
    uint32_t ev=bh_app_tick(a,now);
    a->held_mask=mask&7u;bh_clock_key(&a->clock,bh_key_from_mask(a->held_mask));
    return ev;
}
