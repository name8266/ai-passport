#include "tarot_model.h"

#include <string.h>

static const char *const SPREAD_NAMES[TAROT_SPREAD_COUNT] = {
    "今日指引", "单牌问答", "三牌阵", "凯尔特十字",
};

static const char *const THREE_POSITIONS[] = { "过去", "现在", "未来" };
static const char *const CELTIC_POSITIONS[] = {
    "核心", "挑战", "基础", "近期过去", "可能发展",
    "近期未来", "自我", "环境", "希望与恐惧", "结果",
};

size_t tarot_spread_card_count(tarot_spread_t spread) {
    static const uint8_t counts[TAROT_SPREAD_COUNT] = { 1, 1, 3, 10 };
    return spread >= 0 && spread < TAROT_SPREAD_COUNT ? counts[spread] : 0;
}

const char *tarot_spread_name(tarot_spread_t spread) {
    return spread >= 0 && spread < TAROT_SPREAD_COUNT ? SPREAD_NAMES[spread] : "未知牌阵";
}

const char *tarot_position_name(tarot_spread_t spread, size_t index) {
    if (index >= tarot_spread_card_count(spread)) return "未知牌位";
    if (spread == TAROT_SPREAD_DAILY) return "今日主题";
    if (spread == TAROT_SPREAD_SINGLE) return "问题核心";
    if (spread == TAROT_SPREAD_THREE) return THREE_POSITIONS[index];
    if (spread == TAROT_SPREAD_CELTIC_CROSS) return CELTIC_POSITIONS[index];
    return "未知牌位";
}

bool tarot_session_start(tarot_session_t *session, tarot_spread_t spread,
                         bool reversals_enabled, tarot_random_fn_t random_fn) {
    if (!session || !random_fn || spread < 0 || spread >= TAROT_SPREAD_COUNT) return false;
    uint8_t deck[TAROT_CARD_COUNT];
    for (size_t i = 0; i < TAROT_CARD_COUNT; ++i) deck[i] = (uint8_t)i;
    for (size_t i = TAROT_CARD_COUNT - 1; i > 0; --i) {
        size_t j = random_fn() % (i + 1);
        uint8_t temporary = deck[i];
        deck[i] = deck[j];
        deck[j] = temporary;
    }

    memset(session, 0, sizeof(*session));
    session->spread = spread;
    session->count = (uint8_t)tarot_spread_card_count(spread);
    for (size_t i = 0; i < session->count; ++i) {
        session->cards[i].card_id = deck[i];
        session->cards[i].reversed = reversals_enabled && ((random_fn() & 1U) != 0);
    }
    return true;
}
bool tarot_session_move(tarot_session_t *session, int delta) {
    if (!session || session->count == 0 || delta == 0) return false;
    int next = (int)session->selected + delta;
    while (next < 0) next += session->count;
    while (next >= session->count) next -= session->count;
    session->selected = (uint8_t)next;
    return true;
}

bool tarot_session_reveal(tarot_session_t *session) {
    if (!session || session->selected >= session->count) return false;
    tarot_draw_t *draw = &session->cards[session->selected];
    if (draw->revealed) return false;
    draw->revealed = true;
    return true;
}

size_t tarot_session_revealed_count(const tarot_session_t *session) {
    if (!session) return 0;
    size_t revealed = 0;
    for (size_t i = 0; i < session->count; ++i) {
        if (session->cards[i].revealed) ++revealed;
    }
    return revealed;
}

bool tarot_session_all_revealed(const tarot_session_t *session) {
    return session && session->count > 0 &&
           tarot_session_revealed_count(session) == session->count;
}
