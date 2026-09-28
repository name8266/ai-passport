#include "tarot_history.h"

#include <string.h>

void tarot_history_init(tarot_history_t *history) {
    if (!history) return;
    memset(history, 0, sizeof(*history));
    history->next_sequence = 1;
}

bool tarot_history_add(tarot_history_t *history, const tarot_session_t *session) {
    if (!history || !session || !tarot_session_all_revealed(session)) return false;
    size_t slot = TAROT_HISTORY_CAPACITY;
    if (history->count < TAROT_HISTORY_CAPACITY) {
        slot = history->count++;
    } else {
        uint32_t oldest = UINT32_MAX;
        for (size_t i = 0; i < TAROT_HISTORY_CAPACITY; ++i) {
            if (!history->records[i].favorite && history->records[i].sequence < oldest) {
                oldest = history->records[i].sequence;
                slot = i;
            }
        }
    }
    if (slot == TAROT_HISTORY_CAPACITY) return false;
    tarot_record_t *record = &history->records[slot];
    memset(record, 0, sizeof(*record));
    record->sequence = history->next_sequence++;
    if (history->next_sequence == 0) history->next_sequence = 1;
    record->spread = session->spread;
    record->count = session->count;
    memcpy(record->cards, session->cards, sizeof(session->cards));
    return true;
}

const tarot_record_t *tarot_history_recent(const tarot_history_t *history, size_t newest_index) {
    if (!history || newest_index >= history->count) return NULL;
    const tarot_record_t *candidate = NULL;
    uint32_t below = UINT32_MAX;
    for (size_t rank = 0; rank <= newest_index; ++rank) {
        candidate = NULL;
        uint32_t best = 0;
        for (size_t i = 0; i < history->count; ++i) {
            uint32_t sequence = history->records[i].sequence;
            if (sequence < below && sequence > best) {
                best = sequence;
                candidate = &history->records[i];
            }
        }
        if (!candidate) return NULL;
        below = candidate->sequence;
    }
    return candidate;
}

bool tarot_history_toggle_favorite(tarot_history_t *history, uint32_t sequence) {
    if (!history) return false;
    for (size_t i = 0; i < history->count; ++i) {
        if (history->records[i].sequence == sequence) {
            history->records[i].favorite = !history->records[i].favorite;
            return true;
        }
    }
    return false;
}

void tarot_history_clear_nonfavorites(tarot_history_t *history) {
    if (!history) return;
    size_t write = 0;
    for (size_t read = 0; read < history->count; ++read) {
        if (history->records[read].favorite) {
            if (write != read) history->records[write] = history->records[read];
            ++write;
        }
    }
    memset(&history->records[write], 0,
           (TAROT_HISTORY_CAPACITY - write) * sizeof(history->records[0]));
    history->count = (uint8_t)write;
}
