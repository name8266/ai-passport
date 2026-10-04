#pragma once
#include "stock_core.h"
bool stock_fetch_quotes(const stock_watch_t *watch, stock_quote_t quotes[STOCK_MAX],
                        const char **error);
bool stock_fetch_detail(const char *code, stock_quote_t *quote, stock_bar_t bars[STOCK_BARS],
                        unsigned *count, const char **error);
