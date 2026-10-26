#include "stock_preview.h"
#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    assert(stock_list_move(0, 0, 1) == 0);
    assert(stock_list_move(0, 1, 1) == 1);
    assert(stock_list_move(1, 1, 1) == 0);
    assert(stock_list_move(0, 1, -1) == 1);
    assert(stock_list_move(3, 3, -1) == 2);
    assert(stock_list_move(12, 12, 1) == 0);
    assert(stock_list_first(3, 4, 0) == 1);
    assert(stock_list_first(12, 13, 0) == 10);
    unsigned first = 0;
    for (unsigned selected = 0; selected < 12; selected++) {
        first = stock_list_first(selected, 12, first);
        assert(first <= 9 && selected >= first && selected < first + 3);
    }
    assert(first == 9);
    first = stock_list_first(0, 12, first);
    assert(first == 0);
    assert(stock_list_first(2, 3, 9) == 0);
    assert(stock_list_first(0, 0, 9) == 0);
    assert(stock_list_first(99, 5, 0) == 2);
    for (unsigned count = 0; count <= 13; count++) {
        unsigned last = count > 3 ? count - 3 : 0;
        for (unsigned y = 0; y <= last * STOCK_LIST_PITCH; y++) {
            for (unsigned desired = 0; desired <= last; desired++) {
                stock_scroll_plan_t plan = stock_scroll_plan(y, desired, count, false);
                unsigned end = (unsigned)plan.target_y / STOCK_LIST_PITCH;
                assert(plan.first <= y / STOCK_LIST_PITCH && plan.first <= end);
                assert(end + 2 < plan.first + STOCK_LIST_SLOTS);
                unsigned bottom = (y + STOCK_LIST_PITCH - 1) / STOCK_LIST_PITCH + 2;
                assert(bottom < plan.first + STOCK_LIST_SLOTS);
                if (desired * STOCK_LIST_PITCH > y)
                    assert(plan.target_y > (int)y && end <= desired);
                else if (desired * STOCK_LIST_PITCH < y)
                    assert(plan.target_y < (int)y && end >= desired);
                else
                    assert(!plan.animate);
            }
        }
        stock_scroll_plan_t snap = stock_scroll_plan(0, last, count, true);
        assert(!snap.animate && snap.target_y == (int)(last * STOCK_LIST_PITCH));
    }
    assert(stock_scroll_duration(76) == 144 && stock_scroll_duration(152) == 168);
    stock_quote_t q = {.valid = true, .code = "000001", .stamp = "20261023150000",
                       .previous = 10000};
    stock_minute_t minutes[STOCK_MINUTES];
    for (unsigned i = 0; i < STOCK_MINUTES; i++) {
        unsigned m = i <= 120 ? 570 + i : 780 + i - 121;
        minutes[i] = (stock_minute_t){.hhmm = (uint16_t)(m / 60 * 100 + m % 60),
                                      .price = 10000 + (int)i * 10};
    }
    stock_preview_t p = {0};
    for (unsigned count = 1; count <= STOCK_MINUTES; count++) {
        assert(stock_preview_build(&p, &q, minutes, count, "2026-10-23"));
        assert(p.count <= STOCK_SPARK_POINTS);
        assert(p.slot[p.count - 1] == count - 1);
        assert(p.price[p.count - 1] == minutes[count - 1].price);
    }
    assert(p.count == 61 && p.slot[60] == 241);
    assert(stock_preview_matches(&p, &q));
    strcpy(q.code, "600519");
    assert(!stock_preview_matches(&p, &q));
    strcpy(q.code, "000001");
    stock_preview_t saved = p;
    assert(!stock_preview_build(&p, &q, minutes, 242, "2026-10-22"));
    assert(!memcmp(&p, &saved, sizeof(p))); // Failed fetch cannot destroy last good chart.
    minutes[121].hhmm = 1200;
    assert(!stock_preview_build(&p, &q, minutes, 242, "2026-10-23"));
    minutes[121].hhmm = 1300;
    minutes[122].hhmm = 1300;
    assert(!stock_preview_build(&p, &q, minutes, 242, "2026-10-23"));
    struct { unsigned char before[8], pixels[STOCK_SPARK_BYTES], after[8]; } raster;
    memset(&raster, 0xa5, sizeof(raster));
    for (unsigned i = 0; i < 20000; i++) {
        // Extreme fixed-point prices and flat charts exercise 64-bit projection.
        p.previous = i & 1 ? INT_MAX : 1;
        for (unsigned j = 0; j < p.count; j++)
            p.price[j] = i & 1 ? 1 : INT_MAX;
        stock_preview_raster(&p, raster.pixels);
    }
    for (unsigned i = 0; i < 8; i++)
        assert(raster.before[i] == 0xa5 && raster.after[i] == 0xa5);
    assert(raster.pixels[STOCK_SPARK_BYTES - 1] & 1); // Latest quote reaches 15:00.
    puts("Stock preview: PASS (3-visible viewport, wrap, 242-to-61 samples, date/code isolation, "
         "failure retention, bounded I1 raster, 20000 extreme projections)");
}
