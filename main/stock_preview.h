#pragma once
#include "stock_core.h"

/* One sample per four trading minutes, including the latest partial bucket. */
#define STOCK_SPARK_POINTS 61
#define STOCK_SPARK_WIDTH 200
#define STOCK_SPARK_HEIGHT 24
#define STOCK_SPARK_STRIDE (STOCK_SPARK_WIDTH / 8)
#define STOCK_SPARK_BYTES (STOCK_SPARK_STRIDE * STOCK_SPARK_HEIGHT)
#define STOCK_LIST_VISIBLE 3
#define STOCK_LIST_SLOTS 5
#define STOCK_LIST_PITCH 76
typedef struct {
    char code[7], date[11];
    int32_t previous, price[STOCK_SPARK_POINTS];
    uint8_t slot[STOCK_SPARK_POINTS], count;
} stock_preview_t;
unsigned stock_list_first(unsigned selected, unsigned count, unsigned first);
/* The final selectable row is always Add, after watch_count stocks. */
unsigned stock_list_move(unsigned selected, unsigned watch_count, int direction);
typedef struct {
    unsigned first;
    int32_t target_y;
    bool animate;
} stock_scroll_plan_t;
/* Keep the current and next viewport within the five reusable cards. */
stock_scroll_plan_t stock_scroll_plan(int32_t current_y, unsigned desired_first,
                                     unsigned count, bool snap);
unsigned stock_scroll_duration(unsigned distance);
bool stock_preview_matches(const stock_preview_t *preview, const stock_quote_t *quote);
bool stock_preview_build(stock_preview_t *out, const stock_quote_t *quote,
                         const stock_minute_t *minutes, unsigned count, const char *date);
/* Price line only: allocation-free, MSB-first I1 raster; zero is transparent. */
void stock_preview_raster(const stock_preview_t *preview, uint8_t pixels[STOCK_SPARK_BYTES]);
