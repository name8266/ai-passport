#pragma once
#include "stock_core.h"
// Single network-worker ownership: returned data are copied before the next request.
// quotes must hold watch->count entries; a detail request needs only one entry.
bool stock_fetch_quotes(const stock_watch_t *watch, stock_quote_t *quotes, const char **error);
bool stock_fetch_detail(const char *code, stock_period_t period, stock_quote_t *quote,
                        stock_bar_t bars[STOCK_BARS], unsigned *count,
                        stock_minute_t minutes[STOCK_MINUTES], unsigned *minute_count,
                        char minute_date[11], const char **error);
