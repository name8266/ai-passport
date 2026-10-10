#pragma once
/* Deterministic, allocation-free pending-item retention for ESP32-C3.
 * The language model proposes NEW tasks; it never owns deletion of old tasks.
 */
#include "hub_ai.h"
#include <string.h>

#define HUB_AI_SUMMARY_MAX_UTF8_BYTES 768u
/* Explicit for both AI/filtered paths; avoids reuse of cleared/old tasks. */
static inline void hub_ai_clear_output(hub_ai_digest_t *out) {
    if(out)memset(out,0,sizeof(*out));
}


typedef enum {
    HUB_AI_MERGE_INVALID = 0,
    HUB_AI_MERGE_OK = 1,
    HUB_AI_MERGE_FULL = 2
} hub_ai_merge_result_t;

static inline uint32_t hub_ai_task_id(const hub_ai_task_t *task) {
    if(!task) return 0u;
    uint32_t h=2166136261u;
    for(const unsigned char *p=(const unsigned char *)task->source;*p;p++)
        h=(h^*p)*16777619u;
    h=(h^0xffu)*16777619u;
    for(const unsigned char *p=(const unsigned char *)task->task;*p;p++)
        h=(h^*p)*16777619u;
    return h?h:1u;
}
static inline bool hub_ai_task_same(const hub_ai_task_t *a,
                                    const hub_ai_task_t *b) {
    return a && b && strcmp(a->source,b->source)==0 &&
           strcmp(a->task,b->task)==0;
}

/* Only explicit ACK may discard old items. Overflow declines the entire
 * batch so uncommitted previews are preserved, not silently dropped. */
static inline hub_ai_merge_result_t hub_ai_merge_pending(
       const hub_ai_digest_t *prior,const hub_ai_digest_t *proposal,
       hub_ai_digest_t *out) {
    if(!proposal || !out || proposal==out ||
       proposal->task_count>HUB_AI_TASK_LIMIT || !proposal->summary[0])
        return HUB_AI_MERGE_INVALID;
    const char *ending=memchr(proposal->summary,0,sizeof(proposal->summary));
    if(!ending || (size_t)(ending-proposal->summary)>
                   HUB_AI_SUMMARY_MAX_UTF8_BYTES)
        return HUB_AI_MERGE_INVALID;
    if(prior && !prior->cleared && prior->task_count>HUB_AI_TASK_LIMIT)
        return HUB_AI_MERGE_INVALID;
    memset(out,0,sizeof(*out));
    memcpy(out->summary,proposal->summary,sizeof(out->summary));
    if(prior && !prior->cleared) {
        out->task_count=prior->task_count;
        memcpy(out->tasks,prior->tasks,sizeof(out->tasks));
    }
    for(uint8_t i=0;i<proposal->task_count;i++) {
        const hub_ai_task_t *p=&proposal->tasks[i];
        if(!p->task[0])return HUB_AI_MERGE_INVALID;
        bool exists=false;
        for(uint8_t j=0;j<out->task_count;j++)
            if(hub_ai_task_same(p,&out->tasks[j])) {exists=true;break;}
        if(exists) continue;
        if(out->task_count==HUB_AI_TASK_LIMIT) return HUB_AI_MERGE_FULL;
        out->tasks[out->task_count++]=*p;
    }
    return HUB_AI_MERGE_OK;
}
