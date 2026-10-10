#include "hub_backlog.h"
bool hub_backlog_push(hub_backlog_t *q, uint32_t uid) {
    if(!q) return false;
    for(uint16_t i=0;i<q->count;i++)
        if(q->uids[(q->head+i)%HUB_BACKLOG_CAPACITY]==uid) return true;
    if(q->count==HUB_BACKLOG_CAPACITY) return false;
    q->uids[(q->head+q->count++)%HUB_BACKLOG_CAPACITY]=uid;
    return true;
}
bool hub_backlog_pop(hub_backlog_t *q, uint32_t *uid) {
    if(!q || !uid || !q->count) return false;
    *uid=q->uids[q->head];
    q->head=(q->head+1)%HUB_BACKLOG_CAPACITY;q->count--;
    return true;
}
void hub_backlog_remove(hub_backlog_t *q, uint32_t uid) {
    if(!q) return;
    uint16_t kept=0, count=q->count;
    for(uint16_t i=0;i<count;i++) {
        uint32_t value=q->uids[(q->head+i)%HUB_BACKLOG_CAPACITY];
        if(value!=uid) q->uids[(q->head+kept++)%HUB_BACKLOG_CAPACITY]=value;
    }
    q->count=kept;
}
