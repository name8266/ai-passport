#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define STOCK_MAX 12
#define STOCK_BARS 60
#define STOCK_NAME_BYTES 64
/* Prices are fixed point, 1 unit = 0.001 CNY. */
typedef struct {
    char code[7], name[STOCK_NAME_BYTES], stamp[15];
    int32_t price, previous, change_bp;
    bool valid;
} stock_quote_t;
typedef struct {
    char date[11];
    int32_t open, close, high, low;
} stock_bar_t;
typedef struct {
    char codes[STOCK_MAX][7];
    uint8_t count;
} stock_watch_t;
typedef struct {
    char code[7];
    uint8_t digit;
} stock_wheel_t;
bool stock_symbol(const char *code, char symbol[9]);
bool stock_decimal(const char *text, int32_t *out);
bool stock_parse_quote(const char *body, const char *code, stock_quote_t *out);
bool stock_parse_bar(const char *date, const char *open, const char *close, const char *high,
                     const char *low, stock_bar_t *out);
void stock_wheel_init(stock_wheel_t *wheel, const char *code);
void stock_wheel_turn(stock_wheel_t *wheel, int direction);
bool stock_wheel_next(stock_wheel_t *wheel);
int stock_watch_find(const stock_watch_t *watch, const char *code);
bool stock_watch_add(stock_watch_t *watch, const char *code);
bool stock_watch_remove(stock_watch_t *watch, const char *code);
bool stock_watch_decode(stock_watch_t *watch, const void *data, size_t bytes);
void stock_gbk_name(const char *src, size_t bytes, char *out, size_t cap);
