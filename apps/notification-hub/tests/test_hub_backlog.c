#include <assert.h>
#include <stdio.h>
#include "hub_backlog.h"
int main(void) {
    hub_backlog_t q={0};uint32_t uid;
    for(uint32_t i=0;i<512;i++) assert(hub_backlog_push(&q,i));
    assert(!hub_backlog_push(&q,512));
    assert(hub_backlog_push(&q,300) && q.count==512);
    hub_backlog_remove(&q,123);assert(q.count==511);
    assert(hub_backlog_push(&q,512));
    for(uint32_t i=0;i<513;i++) {
        if(i==123) continue;
        assert(hub_backlog_pop(&q,&uid) && uid==i);
    }
    assert(!hub_backlog_pop(&q,&uid));
    for(uint32_t i=1000;i<1512;i++) assert(hub_backlog_push(&q,i));
    for(uint32_t i=1000;i<1512;i++) assert(hub_backlog_pop(&q,&uid) && uid==i);
    puts("512 UID burst, duplicate coalescing, cancellation, overflow and wraparound: PASS");
}
