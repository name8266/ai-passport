#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include "../firmware/main/hub_ai_debounce.h"

int main(void) {
    hub_ai_debounce_t state={0};
    assert(!hub_ai_debounce_due(&state,0));
    hub_ai_debounce_begin(&state,1000u);
    assert(state.started_ms==1000u);
    assert(state.due_ms==16000u); /* 0.25 min = 15 s */
    assert(hub_ai_debounce_left_ms(&state,1000u)==15000u);
    assert(!hub_ai_debounce_due(&state,15999u));

    hub_ai_debounce_arrived(&state,4000u,1u);
    assert(state.due_ms==25000u); /* first added notice: +0.15 min = 9 s */
    hub_ai_debounce_arrived(&state,8000u,1u);
    assert(state.due_ms==30000u); /* next notice: +5 s */
    hub_ai_debounce_arrived(&state,9000u,2u);
    assert(state.due_ms==40000u); /* next two notices: +5 s each */
    assert(!hub_ai_debounce_due(&state,39999u));
    assert(hub_ai_debounce_due(&state,40000u));
    hub_ai_debounce_arrived(&state,40000u,1u);
    assert(state.due_ms==40000u); /* too late to postpone a due request */

    hub_ai_debounce_reset(&state);
    hub_ai_debounce_begin(&state,0u);
    hub_ai_debounce_arrived(&state,1000u,50u);
    assert(state.due_ms==HUB_AI_SETTLE_MAX_MS);
    assert(!hub_ai_debounce_due(&state,HUB_AI_SETTLE_MAX_MS-1u));
    assert(hub_ai_debounce_due(&state,HUB_AI_SETTLE_MAX_MS));
    hub_ai_debounce_arrived(&state,HUB_AI_SETTLE_MAX_MS-1u,1000u);
    assert(state.due_ms==HUB_AI_SETTLE_MAX_MS); /* hard 2-minute ceiling */
    assert(hub_ai_debounce_left_ms(&state,HUB_AI_SETTLE_MAX_MS)==0u);
    hub_ai_debounce_reset(&state);
    assert(!state.active);
    hub_ai_debounce_begin(&state,300000u);
    assert(state.due_ms==315000u && state.extensions==0u);
    puts("Debounce: 15s +9s +5s per notice, 120s cap, reset: PASS");
    return 0;
}
