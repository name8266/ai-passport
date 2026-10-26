#pragma once
#include "stock_core.h"
// Single network-worker ownership: returned data are copied before the next request.
// quotes must hold watch->count entries; a detail request needs only one entry.
bool stock_fetch_quotes(const stock_watch_t *watch, stock_quote_t *quotes, const char **error);
bool stock_fetch_detail(const char *code, stock_period_t period, stock_quote_t *quote,
                        stock_bar_t bars[STOCK_BARS], unsigned *count,
                        stock_minute_t minutes[STOCK_MINUTES], unsigned *minute_count,
                        char minute_date[11], const char **error);
// Reuse a fresh quote from the batch response instead of requesting it again.
bool stock_fetch_chart_for_quote(const stock_quote_t *quote, stock_period_t period,
                                 stock_bar_t bars[STOCK_BARS], unsigned *count,
                                 stock_minute_t minutes[STOCK_MINUTES], unsigned *minute_count,
                                 char minute_date[11], const char **error);
// Uses the already fetched quote; does not repeat the quote HTTP request.
bool stock_fetch_minutes(const stock_quote_t *quote, stock_minute_t minutes[STOCK_MINUTES],
                         unsigned *count, char date[11], const char **error);
