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
    uint64_t volume;
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

#define STOCK_MINUTES 242
typedef enum { STOCK_INTRADAY, STOCK_DAY, STOCK_WEEK, STOCK_MONTH } stock_period_t;
typedef struct {
    uint16_t hhmm;
    int32_t price;
    uint64_t volume;
} stock_minute_t;
typedef enum { STOCK_SORT_MANUAL, STOCK_SORT_CODE, STOCK_SORT_CHANGE } stock_sort_t;
typedef enum {
    STOCK_ALERT_OFF,
    STOCK_PRICE_ABOVE,
    STOCK_PRICE_BELOW,
    STOCK_PERCENT_ABOVE,
    STOCK_PERCENT_BELOW
} stock_alert_kind_t;
typedef struct {
    char code[7];
    uint8_t kind;
    int32_t target;
    uint32_t fired_date;
} stock_alert_t;
typedef struct {
    uint8_t version, sort, power_save, sound;
} stock_preferences_t;
bool stock_volume(const char *text, uint64_t *out);
bool stock_parse_minute(const char *line, stock_minute_t *out);
int32_t stock_ma(const stock_bar_t *bars, unsigned count, unsigned at, unsigned period);
void stock_order(const stock_watch_t *watch, const stock_quote_t quotes[STOCK_MAX],
                 stock_sort_t sort, uint8_t order[STOCK_MAX]);
bool stock_alert_valid(const stock_alert_t *alert);
bool stock_alert_evaluate(stock_alert_t *alert, const stock_quote_t *quote, bool fresh);
unsigned stock_refresh_seconds(bool power_save, uint32_t idle_seconds, bool has_alerts);
typedef struct {
    stock_watch_t watch;
    stock_preferences_t prefs;
    stock_alert_t alerts[STOCK_MAX];
} stock_config_t;
bool stock_config_valid(const stock_config_t *config);
