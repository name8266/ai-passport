#pragma once
/* Deterministic, allocation-free pending-item retention for ESP32-C3.
 * The language model proposes NEW tasks; it never owns deletion of old tasks.
 */
#include "hub_ai.h"
#include <string.h>

/* A rolling digest is a 1-2 sentence overview, never an ever-growing log. */
#define HUB_AI_SUMMARY_MAX_UTF8_BYTES 420u
static inline bool hub_ai_summary_concise(const char *summary,size_t capacity) {
    if(!summary || !capacity)return false;
    const char *end=(const char *)memchr(summary,0,capacity);
    if(!end || end==summary || (size_t)(end-summary)>HUB_AI_SUMMARY_MAX_UTF8_BYTES)
        return false;
    unsigned sentences=0;
    size_t len=(size_t)(end-summary);
    for(size_t i=0;i<len;i++) {
        unsigned char c=(unsigned char)summary[i];
        if(c=='\n' || c=='\r')return false;
        if(c=='.' && i>0 && i+1<len &&
           summary[i-1]>='0' && summary[i-1]<='9' &&
           summary[i+1]>='0' && summary[i+1]<='9')continue;
        if(c=='.' || c=='?' || c=='!')sentences++;
        if(i+2<len) {
            const unsigned char *u=(const unsigned char *)summary+i;
            if((u[0]==0xE3 && u[1]==0x80 && u[2]==0x82) ||
               (u[0]==0xEF && u[1]==0xBC && (u[2]==0x81 || u[2]==0x9F)))sentences++;
        }
        if(sentences>2)return false;
    }
    return true;
}
/* Stable, allocation-free importance ordering for a five-item C3 display.
 * Explicit deadlines and urgent actions are prioritized; equal scores keep
 * original order. Never delete an earlier unacknowledged task to make space. */
static inline unsigned hub_ai_task_importance(const hub_ai_task_t *t) {
    if(!t)return 0;
    unsigned score=t->due[0]?24u:0u;
    static const char *const urgent[]={
        "紧急","立即","逾期","截止","尽快","今天","明天",
        "urgent","deadline","overdue"
    };
    for(size_t i=0;i<sizeof(urgent)/sizeof(urgent[0]);i++)
        if(strstr(t->task,urgent[i]) || strstr(t->due,urgent[i]))
            score+=10;
    return score;
}
static inline void hub_ai_rank_pending(hub_ai_digest_t *state) {
    if(!state || state->task_count>HUB_AI_TASK_LIMIT)return;
    for(uint8_t i=1;i<state->task_count;i++) {
        hub_ai_task_t entry=state->tasks[i];
        unsigned importance=hub_ai_task_importance(&entry);
        uint8_t j=i;
        while(j>0 && hub_ai_task_importance(&state->tasks[j-1])<importance) {
            state->tasks[j]=state->tasks[j-1];
            j--;
        }
        state->tasks[j]=entry;
    }
}
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

/* A completion request includes a digest revision (checked by the archive
 * worker) and a stable task fingerprint. Refuse collisions rather than
 * removing the wrong item on a 32-bit hash collision. */
static inline bool hub_ai_complete_pending(hub_ai_digest_t *state,
                                            uint32_t fingerprint) {
    if(!state || state->cleared || !state->summary[0] || !fingerprint ||
       state->task_count==0 || state->task_count>HUB_AI_TASK_LIMIT)
        return false;
    int match=-1;
    for(uint8_t i=0;i<state->task_count;i++) {
        if(hub_ai_task_id(&state->tasks[i])!=fingerprint)continue;
        if(match>=0)return false;
        match=i;
    }
    if(match<0)return false;
    for(uint8_t i=(uint8_t)match;i+1u<state->task_count;i++)
        state->tasks[i]=state->tasks[i+1u];
    state->task_count--;
    memset(&state->tasks[state->task_count],0,sizeof(state->tasks[0]));
    return true;
}

/* Existing ID can gain a newly confirmed deadline but must never lose it
 * because a model omitted the field in a subsequent response. */
static inline void hub_ai_update_known_due(hub_ai_task_t *existing,
                                            const hub_ai_task_t *proposal) {
    if(existing && proposal && proposal->due[0])
        memcpy(existing->due,proposal->due,sizeof(existing->due));
}

/* Only explicit ACK may discard old items. Overflow declines the entire
 * batch so uncommitted previews are preserved, not silently dropped. */
static inline hub_ai_merge_result_t hub_ai_merge_pending(
       const hub_ai_digest_t *prior,const hub_ai_digest_t *proposal,
       hub_ai_digest_t *out) {
    if(!proposal || !out || proposal==out ||
       proposal->task_count>HUB_AI_TASK_LIMIT || !proposal->summary[0])
        return HUB_AI_MERGE_INVALID;
    if(!hub_ai_summary_concise(proposal->summary,sizeof(proposal->summary)))
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
            if(hub_ai_task_same(p,&out->tasks[j])) {
                hub_ai_update_known_due(&out->tasks[j],p);
                exists=true;break;
            }
        if(exists) continue;
        if(out->task_count==HUB_AI_TASK_LIMIT) return HUB_AI_MERGE_FULL;
        out->tasks[out->task_count++]=*p;
    }
    hub_ai_rank_pending(out);
    return HUB_AI_MERGE_OK;
}
