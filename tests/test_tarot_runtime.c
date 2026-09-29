#include <assert.h>
#include <stdio.h>

#include "tarot_runtime.h"

static void test_idle_boundaries(void) {
    tarot_runtime_t runtime;
    tarot_runtime_init(&runtime, 0);
    assert(tarot_runtime_update_idle(&runtime, TAROT_DIM_AFTER_US - 1) ==
           TAROT_IDLE_ACTIVE);
    assert(tarot_runtime_update_idle(&runtime, TAROT_DIM_AFTER_US) ==
           TAROT_IDLE_DIMMED);
    assert(tarot_runtime_update_idle(&runtime, TAROT_OFF_AFTER_US - 1) ==
           TAROT_IDLE_DIMMED);
    assert(tarot_runtime_update_idle(&runtime, TAROT_OFF_AFTER_US) ==
           TAROT_IDLE_OFF);
}

static void test_dimmed_press_still_acts(void) {
    tarot_runtime_t runtime;
    bool woke = false;
    tarot_runtime_init(&runtime, 0);
    assert(tarot_runtime_update_idle(&runtime, TAROT_DIM_AFTER_US) ==
           TAROT_IDLE_DIMMED);
    assert(!tarot_runtime_note_input(&runtime, TAROT_DIM_AFTER_US + 1, true, &woke));
    assert(!woke);
    assert(tarot_runtime_note_input(&runtime, TAROT_DIM_AFTER_US + 2, false, &woke));
}

static void test_off_first_interaction_is_wake_only(void) {
    tarot_runtime_t runtime;
    bool woke = false;
    tarot_runtime_init(&runtime, 0);
    assert(tarot_runtime_update_idle(&runtime, TAROT_OFF_AFTER_US) == TAROT_IDLE_OFF);

    /* PRESS and every later event from the same physical interaction are
     * swallowed. The next PRESS re-arms normal CLICK/LONG dispatch. */
    assert(!tarot_runtime_note_input(&runtime, TAROT_OFF_AFTER_US + 1, true, &woke));
    assert(woke);
    assert(!tarot_runtime_note_input(&runtime, TAROT_OFF_AFTER_US + 2, false, &woke));
    assert(!woke);
    assert(!tarot_runtime_note_input(&runtime, TAROT_OFF_AFTER_US + 3, false, &woke));
    assert(!tarot_runtime_note_input(&runtime, TAROT_OFF_AFTER_US + 4, true, &woke));
    assert(tarot_runtime_note_input(&runtime, TAROT_OFF_AFTER_US + 5, false, &woke));
}

static void test_off_click_without_press_is_still_suppressed(void) {
    tarot_runtime_t runtime;
    bool woke = false;
    tarot_runtime_init(&runtime, 0);
    assert(tarot_runtime_update_idle(&runtime, TAROT_OFF_AFTER_US) == TAROT_IDLE_OFF);
    assert(!tarot_runtime_note_input(&runtime, TAROT_OFF_AFTER_US + 1, false, &woke));
    assert(woke);
    assert(!tarot_runtime_note_input(&runtime, TAROT_OFF_AFTER_US + 2, true, &woke));
    assert(tarot_runtime_note_input(&runtime, TAROT_OFF_AFTER_US + 3, false, &woke));
}

static void test_battery_refresh_schedule(void) {
    tarot_runtime_t runtime;
    tarot_runtime_init(&runtime, 0);
    assert(!tarot_runtime_battery_refresh_due(&runtime, TAROT_BATTERY_REFRESH_US - 1));
    assert(tarot_runtime_battery_refresh_due(&runtime, TAROT_BATTERY_REFRESH_US));
    assert(!tarot_runtime_battery_refresh_due(&runtime, TAROT_BATTERY_REFRESH_US + 1));
    assert(tarot_runtime_battery_refresh_due(&runtime, 2 * TAROT_BATTERY_REFRESH_US));
}

int main(void) {
    test_idle_boundaries();
    test_dimmed_press_still_acts();
    test_off_first_interaction_is_wake_only();
    test_off_click_without_press_is_still_suppressed();
    test_battery_refresh_schedule();
    puts("tarot runtime tests passed");
    return 0;
}
