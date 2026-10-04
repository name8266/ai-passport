#include "stock_core.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
extern const uint16_t stock_gbk_unicode[126 * 191];
bool stock_symbol(const char *code, char symbol[9]) {
    if (!code || strlen(code) != 6)
        return false;
    for (int i = 0; i < 6; i++)
        if (code[i] < '0' || code[i] > '9')
            return false;
    const char *market = NULL;
    if (code[0] == '6')
        market = "sh";
    else if (code[0] == '0' || code[0] == '3')
        market = "sz";
    else if (code[0] == '4' || code[0] == '8' || strncmp(code, "920", 3) == 0)
        market = "bj";
    if (!market || strcmp(code, "000000") == 0)
        return false;
    if (symbol)
        snprintf(symbol, 9, "%s%s", market, code);
    return true;
}
bool stock_decimal(const char *s, int32_t *out) {
    if (!s || !*s || !out)
        return false;
    bool neg = *s == '-';
    if (neg || *s == '+')
        s++;
    if (*s < '0' || *s > '9')
        return false;
    int64_t v = 0;
    while (*s >= '0' && *s <= '9') {
        v = v * 10 + (*s++ - '0');
        if (v > INT32_MAX / 1000)
            return false;
    }
    v *= 1000;
    if (*s == '.') {
        s++;
        int factor = 100;
        if (*s < '0' || *s > '9')
            return false;
        while (*s >= '0' && *s <= '9') {
            if (factor) {
                v += (*s - '0') * factor;
                factor /= 10;
            }
            s++;
        }
    }
    if (*s || v > INT32_MAX)
        return false;
    *out = neg ? -(int32_t)v : (int32_t)v;
    return true;
}
void stock_gbk_name(const char *src, size_t bytes, char *out, size_t cap) {
    size_t p = 0;
    if (!cap)
        return;
    for (size_t i = 0; i < bytes;) {
        unsigned a = (unsigned char)src[i++], u = a;
        if (a >= 0x81 && a <= 0xfe && i < bytes) {
            unsigned b = (unsigned char)src[i++];
            u = (b >= 0x40 && b <= 0xfe && b != 0x7f)
                    ? stock_gbk_unicode[(a - 0x81) * 191 + b - 0x40]
                    : 0;
        } else if (a >= 128)
            u = 0;
        if (!u)
            u = '?';
        size_t n = u < 128 ? 1 : u < 2048 ? 2 : 3;
        if (p + n >= cap)
            break;
        if (n == 1)
            out[p++] = (char)u;
        else if (n == 2) {
            out[p++] = (char)(0xc0 | (u >> 6));
            out[p++] = (char)(0x80 | (u & 63));
        } else {
            out[p++] = (char)(0xe0 | (u >> 12));
            out[p++] = (char)(0x80 | ((u >> 6) & 63));
            out[p++] = (char)(0x80 | (u & 63));
        }
    }
    out[p] = 0;
}
bool stock_parse_quote(const char *body, const char *code, stock_quote_t *out) {
    char symbol[9], key[24];
    if (!body || !out || !stock_symbol(code, symbol))
        return false;
    snprintf(key, sizeof(key), "v_%s=\"", symbol);
    const char *p = strstr(body, key);
    if (!p)
        return false;
    p += strlen(key);
    const char *end = strchr(p, '"');
    if (!end || end - p > 1800)
        return false;
    stock_quote_t q = {0};
    memcpy(q.code, code, 7);
    int f = 0;
    bool price = false, previous = false, matched = false;
    while (p <= end) {
        const char *next = memchr(p, '~', (size_t)(end - p));
        if (!next)
            next = end;
        size_t len = (size_t)(next - p);
        char field[80];
        if (len >= sizeof(field) && f != 1)
            return false;
        if (f == 1)
            stock_gbk_name(p, len, q.name, sizeof(q.name));
        else {
            memcpy(field, p, len);
            field[len] = 0;
            if (f == 2)
                matched = !strcmp(field, code);
            if (f == 3)
                price = stock_decimal(field, &q.price);
            if (f == 4)
                previous = stock_decimal(field, &q.previous);
            if (f == 30 && len == 14) {
                bool digits = true;
                for (size_t i = 0; i < len; i++)
                    digits &= field[i] >= '0' && field[i] <= '9';
                if (digits)
                    memcpy(q.stamp, field, 15);
            }
        }
        f++;
        if (next == end)
            break;
        p = next + 1;
    }
    if (!matched || !price || !previous || q.previous <= 0 || q.price <= 0 || !q.stamp[0])
        return false;
    int64_t change = ((int64_t)q.price - q.previous) * 10000;
    q.change_bp =
        (int32_t)((change + (change >= 0 ? q.previous / 2 : -q.previous / 2)) / q.previous);
    q.valid = true;
    *out = q;
    return true;
}
bool stock_parse_bar(const char *date, const char *open, const char *close, const char *high,
                     const char *low, stock_bar_t *out) {
    if (!date || strlen(date) != 10 || date[4] != '-' || date[7] != '-' || !out)
        return false;
    for (int i = 0; i < 10; i++)
        if (i != 4 && i != 7 && (date[i] < '0' || date[i] > '9'))
            return false;
    stock_bar_t b = {0};
    memcpy(b.date, date, 11);
    if (!stock_decimal(open, &b.open) || !stock_decimal(close, &b.close) ||
        !stock_decimal(high, &b.high) || !stock_decimal(low, &b.low))
        return false;
    if (b.low <= 0 || b.high < b.low || b.open < b.low || b.open > b.high || b.close < b.low ||
        b.close > b.high)
        return false;
    *out = b;
    return true;
}
void stock_wheel_init(stock_wheel_t *w, const char *code) {
    memcpy(w->code, code && strlen(code) == 6 ? code : "000001", 7);
    w->digit = 0;
}
void stock_wheel_turn(stock_wheel_t *w, int d) {
    int n = w->code[w->digit] - '0';
    w->code[w->digit] = (char)('0' + (n + (d < 0 ? 9 : 1)) % 10);
}
bool stock_wheel_next(stock_wheel_t *w) {
    if (w->digit == 5)
        return true;
    w->digit++;
    return false;
}
int stock_watch_find(const stock_watch_t *w, const char *code) {
    for (int i = 0; i < w->count; i++)
        if (!strcmp(w->codes[i], code))
            return i;
    return -1;
}
bool stock_watch_add(stock_watch_t *w, const char *code) {
    if (!stock_symbol(code, NULL) || stock_watch_find(w, code) >= 0 || w->count >= STOCK_MAX)
        return false;
    memcpy(w->codes[w->count++], code, 7);
    return true;
}
bool stock_watch_remove(stock_watch_t *w, const char *code) {
    int i = stock_watch_find(w, code);
    if (i < 0)
        return false;
    memmove(w->codes[i], w->codes[i + 1], (w->count - i - 1) * 7);
    w->count--;
    memset(w->codes[w->count], 0, 7);
    return true;
}
bool stock_watch_decode(stock_watch_t *w, const void *data, size_t bytes) {
    if (bytes != sizeof(*w) || !data)
        return false;
    stock_watch_t t;
    memcpy(&t, data, bytes);
    if (t.count > STOCK_MAX)
        return false;
    stock_watch_t clean = {0};
    for (int i = 0; i < t.count; i++) {
        if (!memchr(t.codes[i], 0, 7) || !stock_watch_add(&clean, t.codes[i]))
            return false;
    }
    *w = clean;
    return true;
}
