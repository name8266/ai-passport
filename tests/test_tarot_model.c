#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "tarot_model.h"
#include "tarot_history.h"

static uint32_t s_random_state = 0xA341316CU;

static uint32_t test_random(void) {
    s_random_state ^= s_random_state << 13;
    s_random_state ^= s_random_state >> 17;
    s_random_state ^= s_random_state << 5;
    return s_random_state;
}

static void test_spread_shapes(void) {
    assert(tarot_spread_card_count(TAROT_SPREAD_DAILY) == 1);
    assert(tarot_spread_card_count(TAROT_SPREAD_SINGLE) == 1);
    assert(tarot_spread_card_count(TAROT_SPREAD_THREE) == 3);
    assert(tarot_spread_card_count(TAROT_SPREAD_CELTIC_CROSS) == 10);
    assert(strcmp(tarot_position_name(TAROT_SPREAD_THREE, 2), "未来") == 0);
    assert(strcmp(tarot_position_name(TAROT_SPREAD_CELTIC_CROSS, 9), "结果") == 0);
}

static void test_unique_draws_and_reveal(void) {
    tarot_session_t session;
    assert(tarot_session_start(&session, TAROT_SPREAD_CELTIC_CROSS, true, test_random));
    assert(session.count == 10);
    bool seen[TAROT_CARD_COUNT] = { false };
    for (size_t i = 0; i < session.count; ++i) {
        assert(session.cards[i].card_id < TAROT_CARD_COUNT);
        assert(!seen[session.cards[i].card_id]);
        seen[session.cards[i].card_id] = true;
        assert(!session.cards[i].revealed);
    }
    assert(!tarot_session_all_revealed(&session));
    for (size_t i = 0; i < session.count; ++i) {
        session.selected = (uint8_t)i;
        assert(tarot_session_reveal(&session));
        assert(!tarot_session_reveal(&session));
    }
    assert(tarot_session_all_revealed(&session));
}

static void test_navigation_wraps_and_reversals_can_be_disabled(void) {
    tarot_session_t session;
    assert(tarot_session_start(&session, TAROT_SPREAD_THREE, false, test_random));
    for (size_t i = 0; i < session.count; ++i) assert(!session.cards[i].reversed);
    assert(tarot_session_move(&session, -1));
    assert(session.selected == 2);
    assert(tarot_session_move(&session, 1));
    assert(session.selected == 0);
}

static void test_history_preserves_favorites(void) {
    tarot_history_t history;
    tarot_history_init(&history);
    tarot_session_t session;
    for (size_t item = 0; item < TAROT_HISTORY_CAPACITY; ++item) {
        assert(tarot_session_start(&session, TAROT_SPREAD_DAILY, false, test_random));
        assert(tarot_session_reveal(&session));
        assert(tarot_history_add(&history, &session));
    }
    const tarot_record_t *oldest = tarot_history_recent(&history, TAROT_HISTORY_CAPACITY - 1);
    assert(oldest);
    uint32_t favorite_sequence = oldest->sequence;
    assert(tarot_history_toggle_favorite(&history, favorite_sequence));
    assert(tarot_session_start(&session, TAROT_SPREAD_SINGLE, true, test_random));
    assert(tarot_session_reveal(&session));
    assert(tarot_history_add(&history, &session));
    bool found = false;
    for (size_t i = 0; i < history.count; ++i) {
        if (history.records[i].sequence == favorite_sequence) found = true;
    }
    assert(found);
    tarot_history_clear_nonfavorites(&history);
    assert(history.count == 1);
    assert(history.records[0].favorite);
}

int main(void) {
    test_spread_shapes();
    test_unique_draws_and_reveal();
    test_navigation_wraps_and_reversals_can_be_disabled();
    test_history_preserves_favorites();
    puts("tarot model tests passed");
    return 0;
}
