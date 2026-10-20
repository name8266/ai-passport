#include "brake_scores.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
static void put(uint8_t *p, uint32_t n) { for (int i = 0; i < 4; ++i) p[i] = (uint8_t)(n >> (8*i)); }
static uint32_t get(const uint8_t *p) { return p[0] | (uint32_t)p[1]<<8 | (uint32_t)p[2]<<16 | (uint32_t)p[3]<<24; }
static uint32_t crc(const uint8_t *p, size_t n) {
    uint32_t c = UINT32_MAX;
    for (size_t i = 0; i < n; ++i) {
        c ^= p[i];
        for (int j = 0; j < 8; ++j) c = (c >> 1) ^ (0xedb88320u & (0u - (c & 1)));
    }
    return ~c;
}
void bh_scores_clear(bh_scores_t *t) { memset(t, 0, sizeof(*t)); t->next_sequence = 1; }
static bool before(const bh_record_t *a, const bh_record_t *b) {
    if (a->peak_mms != b->peak_mms) return a->peak_mms > b->peak_mms;
    if (a->remaining_mm != b->remaining_mm) return a->remaining_mm < b->remaining_mm;
    return a->sequence < b->sequence;
}
int bh_scores_add(bh_scores_t *t, const bh_state_t *s) {
    if (!bh_rankable(s) || !isfinite(s->remaining) || !isfinite(s->max_pedal) ||
        s->remaining < 0 || s->remaining > BH_TRACK_M || s->peak_speed > 51 ||
        s->max_pedal < 0 || s->max_pedal > 1620) return -1;
    /* Rebase sequence safely before wrap; current order is the stable tie rule. */
    if (t->next_sequence == UINT32_MAX) {
        for (unsigned i = 0; i < t->count; ++i) t->rows[i].sequence = i + 1;
        t->next_sequence = t->count + 1;
    }
    bh_record_t r = { .peak_mms = (uint32_t)lroundf(s->peak_speed * 1000),
        .remaining_mm = (uint32_t)lroundf(s->remaining * 1000),
        .max_pedal_n = (uint32_t)lroundf(s->max_pedal), .sequence = t->next_sequence++,
        .tent_used = s->max_tent > 0, .obstacle = s->obstacle };
    memcpy(r.name, "ANON", 5);
    int pos = 0;
    while (pos < t->count && !before(&r, &t->rows[pos])) ++pos;
    if (pos == BH_TOP) return -1;
    if (t->count < BH_TOP) ++t->count;
    for (int i = t->count - 1; i > pos; --i) t->rows[i] = t->rows[i-1];
    t->rows[pos] = r;
    return pos;
}
void bh_scores_encode(const bh_scores_t *t, uint8_t data[BH_SCORE_BYTES]) {
    memset(data, 0, BH_SCORE_BYTES);
    memcpy(data, "BHR1", 4); put(data+4, t->next_sequence); data[8] = t->count;
    for (unsigned i = 0; i < t->count; ++i) {
        const bh_record_t *r = &t->rows[i]; uint8_t *p = data + 12 + i*32;
        put(p, r->peak_mms); put(p+4, r->remaining_mm); put(p+8, r->max_pedal_n);
        put(p+12, r->sequence); memcpy(p+16, r->name, 8); p[24] = r->tent_used; p[25] = r->obstacle;
    }
    put(data + BH_SCORE_BYTES - 4, crc(data, BH_SCORE_BYTES - 4));
}
bool bh_scores_decode(bh_scores_t *t, const uint8_t *data, size_t size) {
    bh_scores_clear(t);
    if (!data || size != BH_SCORE_BYTES || memcmp(data, "BHR1", 4) || data[8] > BH_TOP ||
        !get(data+4) || get(data+BH_SCORE_BYTES-4) != crc(data, BH_SCORE_BYTES-4)) return false;
    bh_scores_t tmp = { .count = data[8], .next_sequence = get(data+4) };
    for (unsigned i = 0; i < tmp.count; ++i) {
        const uint8_t *p = data+12+i*32; bh_record_t *r = &tmp.rows[i];
        *r = (bh_record_t){ .peak_mms = get(p), .remaining_mm = get(p+4),
            .max_pedal_n = get(p+8), .sequence = get(p+12), .tent_used = p[24], .obstacle = p[25] };
        memcpy(r->name, p+16, 8);
        if (r->peak_mms < 27778 || r->peak_mms > 51000 || r->remaining_mm > 500000 ||
            r->max_pedal_n > 1620 || r->obstacle > 3 || r->tent_used > 1 ||
            !r->sequence || r->sequence >= tmp.next_sequence || !memchr(r->name, 0, 8) ||
            (i && before(r, &tmp.rows[i-1]))) return false;
        for (unsigned j = 0; j < 8 && r->name[j]; ++j) if (r->name[j] < 32 || r->name[j] > 126) return false;
    }
    *t = tmp;
    return true;
}
