#pragma once
/* Fixed-size notification burst coalescer for ESP32-C3: no heap allocation,
 * timers or RTOS objects. All values are monotonic milliseconds.
 *
 * First eligible group: 15 s.
 * First later notification within the window: extend by 9 s.
 * Further new notifications: extend by 5 s each.
 * Never postpone beyond 120 s from the first eligible group.
 */
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#define HUB_AI_SETTLE_FIRST_MS 15000u
#define HUB_AI_SETTLE_SECOND_MS 9000u
#define HUB_AI_SETTLE_FOLLOW_MS 5000u
#define HUB_AI_SETTLE_MAX_MS 120000u

typedef struct {
    uint64_t started_ms;
    uint64_t due_ms;
    uint8_t extensions;
    bool active;
} hub_ai_debounce_t;

static inline void hub_ai_debounce_reset(hub_ai_debounce_t *state) {
    if(state)memset(state,0,sizeof(*state));
}
static inline void hub_ai_debounce_begin(hub_ai_debounce_t *state,
                                          uint64_t now_ms) {
    if(!state)return;
    state->started_ms=now_ms;
    state->due_ms=now_ms+HUB_AI_SETTLE_FIRST_MS;
    state->extensions=0;
    state->active=true;
}
static inline void hub_ai_debounce_arrived(hub_ai_debounce_t *state,
                                            uint64_t now_ms,
                                            uint32_t new_previews) {
    if(!state || !state->active || !new_previews ||
       now_ms>=state->due_ms)return;
    uint64_t cap=state->started_ms+HUB_AI_SETTLE_MAX_MS;
    while(new_previews-- && state->due_ms<cap) {
        uint64_t step=state->extensions?HUB_AI_SETTLE_FOLLOW_MS:
                                           HUB_AI_SETTLE_SECOND_MS;
        if(state->extensions<UINT8_MAX)state->extensions++;
        uint64_t target=state->due_ms+step;
        state->due_ms=target<cap?target:cap;
    }
}
static inline bool hub_ai_debounce_due(const hub_ai_debounce_t *state,
                                       uint64_t now_ms) {
    return state && state->active && now_ms>=state->due_ms;
}
static inline uint32_t hub_ai_debounce_left_ms(const hub_ai_debounce_t *state,
                                                uint64_t now_ms) {
    if(!state || !state->active || now_ms>=state->due_ms)return 0;
    return (uint32_t)(state->due_ms-now_ms);
}
