#include "stock_network.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include <stdio.h>
#include <string.h>
#define RESPONSE_LIMIT 18000
// Only stocks_net calls this module; TLS and parsing share one reserved response arena.
static char response_body[RESPONSE_LIMIT + 1];
static char *get(const char *url, const char **error) {
    esp_http_client_config_t cfg = {.url = url,
                                    .timeout_ms = 6000,
                                    .buffer_size = 1024,
                                    .buffer_size_tx = 512,
                                    .crt_bundle_attach = esp_crt_bundle_attach,
                                    .user_agent = "PassportStocks/1.0"};
    esp_http_client_handle_t c = esp_http_client_init(&cfg);
    char *body = NULL;
    if (!c) {
        *error = "连接内存不足";
        return NULL;
    }
    esp_http_client_set_header(c, "Referer", "https://gu.qq.com/");
    if (esp_http_client_open(c, 0) != ESP_OK || esp_http_client_fetch_headers(c) < 0) {
        *error = "行情连接失败";
        goto done;
    }
    if (esp_http_client_get_status_code(c) != 200) {
        *error = "行情服务暂不可用";
        goto done;
    }
    body = response_body;
    size_t used = 0;
    int n;
    while (used < RESPONSE_LIMIT &&
           (n = esp_http_client_read(c, body + used, RESPONSE_LIMIT - used)) > 0)
        used += (size_t)n;
    if (used == RESPONSE_LIMIT || !esp_http_client_is_complete_data_received(c)) {
        *error = "行情响应不完整";
        body = NULL;
        goto done;
    }
    body[used] = 0;
done:
    esp_http_client_close(c);
    esp_http_client_cleanup(c);
    return body;
}

// Bounded JSON views: validate the entire response, then read only chart fields.
// Metadata and arrays never become heap-allocated cJSON nodes.
typedef struct {
    const char *start, *end;
} json_span_t;
static void json_space(const char **p, const char *end) {
    while (*p < end && (**p == ' ' || **p == '\n' || **p == '\r' || **p == '\t'))
        (*p)++;
}
static int json_hex(char c) {
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}
static bool json_quoted(const char **p, const char *end) {
    if (*p == end || *(*p)++ != '"')
        return false;
    while (*p < end) {
        unsigned char c = (unsigned char)*(*p)++;
        if (c == '"')
            return true;
        if (c < 32)
            return false;
        if (c != '\\')
            continue;
        if (*p == end)
            return false;
        c = (unsigned char)*(*p)++;
        if (c == 'u') {
            for (unsigned i = 0; i < 4; i++)
                if (*p == end || json_hex(*(*p)++) < 0)
                    return false;
        } else if (!strchr("\"\\/bfnrt", c))
            return false;
    }
    return false;
}
static bool json_value(const char **p, const char *end, unsigned depth, json_span_t *out) {
    json_space(p, end);
    if (*p == end || depth > 24)
        return false;
    const char *start = *p;
    char c = **p;
    if (c == '"') {
        if (!json_quoted(p, end))
            return false;
    } else if (c == '[' || c == '{') {
        char close = c == '[' ? ']' : '}';
        (*p)++;
        json_space(p, end);
        if (*p < end && **p == close)
            (*p)++;
        else
            for (;;) {
                json_span_t ignored;
                if (c == '{') {
                    if (!json_quoted(p, end))
                        return false;
                    json_space(p, end);
                    if (*p == end || *(*p)++ != ':')
                        return false;
                }
                if (!json_value(p, end, depth + 1, &ignored))
                    return false;
                json_space(p, end);
                if (*p == end)
                    return false;
                char separator = *(*p)++;
                if (separator == close)
                    break;
                if (separator != ',')
                    return false;
                json_space(p, end);
            }
    } else if (c == '-' || (c >= '0' && c <= '9')) {
        if (c == '-')
            (*p)++;
        if (*p == end)
            return false;
        if (**p == '0')
            (*p)++;
        else {
            if (**p < '1' || **p > '9')
                return false;
            do {
                (*p)++;
            } while (*p < end && **p >= '0' && **p <= '9');
        }
        if (*p < end && **p == '.') {
            (*p)++;
            const char *digits = *p;
            while (*p < end && **p >= '0' && **p <= '9')
                (*p)++;
            if (*p == digits)
                return false;
        }
        if (*p < end && (**p == 'e' || **p == 'E')) {
            (*p)++;
            if (*p < end && (**p == '+' || **p == '-'))
                (*p)++;
            const char *digits = *p;
            while (*p < end && **p >= '0' && **p <= '9')
                (*p)++;
            if (*p == digits)
                return false;
        }
    } else {
        const char *literal = c == 't' ? "true" : c == 'f' ? "false" : c == 'n' ? "null" : NULL;
        if (!literal || end - *p < (int)strlen(literal) || strncmp(*p, literal, strlen(literal)))
            return false;
        *p += strlen(literal);
    }
    *out = (json_span_t){start, *p};
    return true;
}
static bool json_string(json_span_t span, char *out, size_t cap) {
    if (!span.start || *span.start != '"' || !cap)
        return false;
    size_t n = 0;
    for (const char *p = span.start + 1; p < span.end - 1; p++) {
        unsigned char c = (unsigned char)*p;
        if (c == '\\') {
            c = (unsigned char)*++p;
            if (c == 'u') {
                unsigned value = 0;
                for (unsigned i = 0; i < 4; i++)
                    value = value * 16 + (unsigned)json_hex(*++p);
                if (!value || value > 127)
                    return false;
                c = (unsigned char)value;
            } else if (c == 'b')
                c = '\b';
            else if (c == 'f')
                c = '\f';
            else if (c == 'n')
                c = '\n';
            else if (c == 'r')
                c = '\r';
            else if (c == 't')
                c = '\t';
        }
        if (n + 1 >= cap)
            return false;
        out[n++] = (char)c;
    }
    out[n] = 0;
    return true;
}
static bool json_field(json_span_t object, const char *name, json_span_t *out) {
    if (*object.start != '{')
        return false;
    const char *p = object.start + 1;
    json_space(&p, object.end);
    while (p < object.end && *p != '}') {
        json_span_t key, value;
        if (!json_value(&p, object.end, 0, &key))
            return false;
        json_space(&p, object.end);
        p++;
        if (!json_value(&p, object.end, 0, &value))
            return false;
        char text[32];
        if (json_string(key, text, sizeof(text)) && !strcmp(text, name)) {
            *out = value;
            return true;
        }
        json_space(&p, object.end);
        if (p < object.end && *p == ',') {
            p++;
            json_space(&p, object.end);
        }
    }
    return false;
}
static bool json_next(const char **p, const char *end, json_span_t *out) {
    json_space(p, end);
    if (*p == end || **p == ']')
        return false;
    if (**p == ',') {
        (*p)++;
        json_space(p, end);
    }
    return json_value(p, end, 0, out);
}

bool stock_fetch_quotes(const stock_watch_t *w, stock_quote_t *quotes, const char **error) {
    if (!w->count)
        return true;
    char url[256] = "https://qt.gtimg.cn/q=";
    for (int i = 0; i < w->count; i++) {
        char symbol[9];
        if (!stock_symbol(w->codes[i], symbol)) {
            *error = "股票代码无效";
            return false;
        }
        strcat(url, i ? "," : "");
        strcat(url, symbol);
    }
    char *body = get(url, error);
    if (!body)
        return false;
    bool all = true;
    for (int i = 0; i < w->count; i++) {
        if (!stock_parse_quote(body, w->codes[i], &quotes[i])) {
            all = false;
            *error = "部分代码无行情";
        }
    }
    return all;
}
bool stock_fetch_detail(const char *code, stock_period_t period, stock_quote_t *q,
                        stock_bar_t bars[STOCK_BARS], unsigned *count,
                        stock_minute_t minutes[STOCK_MINUTES], unsigned *minute_count,
                        char minute_date[11], const char **error) {
    stock_watch_t one = {0};
    stock_watch_add(&one, code);
    if (!stock_fetch_quotes(&one, q, error))
        return false;
    char symbol[9], url[200];
    if (!stock_symbol(code, symbol)) {
        *error = "股票代码无效";
        return false;
    }
    const char *unit = period == STOCK_WEEK ? "week" : period == STOCK_MONTH ? "month" : "day";
    if (period == STOCK_INTRADAY)
        snprintf(url, sizeof(url), "https://web.ifzq.gtimg.cn/appstock/app/minute/query?code=%s",
                 symbol);
    else
        snprintf(url, sizeof(url),
                 "https://web.ifzq.gtimg.cn/appstock/app/fqkline/get?param=%s,%s,,,60,qfq", symbol,
                 unit);
    char *body = get(url, error);
    if (!body)
        return false;
    json_span_t root, rc, data, stock;
    const char *end = body + strlen(body), *at = body;
    bool valid = json_value(&at, end, 0, &root);
    json_space(&at, end);
    valid = valid && at == end && json_field(root, "code", &rc) && rc.end - rc.start == 1 &&
            *rc.start == '0' && json_field(root, "data", &data) && json_field(data, symbol, &stock);
    if (!valid) {
        *error = "行情数据不可用";
        return false;
    }
    if (period == STOCK_INTRADAY) {
        json_span_t series, items, date;
        char date_text[9];
        valid = json_field(stock, "data", &series) && json_field(series, "data", &items) &&
                *items.start == '[' && json_field(series, "date", &date) &&
                json_string(date, date_text, sizeof(date_text)) && strlen(date_text) == 8 &&
                !strncmp(date_text, q->stamp, 8);
        unsigned kept = 0, total = 0;
        const char *next = valid ? items.start + 1 : NULL;
        json_span_t item;
        while (valid && json_next(&next, items.end, &item)) {
            char line[96];
            if (++total > 512 || !json_string(item, line, sizeof(line))) {
                valid = false;
                break;
            }
            unsigned hhmm = 0;
            if (sscanf(line, "%4u", &hhmm) != 1) {
                valid = false;
                break;
            }
            if (hhmm < 930 || hhmm > 1500 || (hhmm > 1130 && hhmm < 1300))
                continue;
            if (kept >= STOCK_MINUTES || !stock_parse_minute(line, &minutes[kept]) ||
                (kept && (minutes[kept].hhmm <= minutes[kept - 1].hhmm ||
                          minutes[kept].volume < minutes[kept - 1].volume))) {
                valid = false;
                break;
            }
            kept++;
        }
        if (valid && kept) {
            snprintf(minute_date, 11, "%.4s-%.2s-%.2s", date_text, date_text + 4, date_text + 6);
            *minute_count = kept;
            return true;
        }
        *error = "分时数据不可用";
        return false;
    }
    char key[16];
    snprintf(key, sizeof(key), "qfq%s", unit);
    json_span_t rows;
    valid = json_field(stock, key, &rows) && *rows.start == '[';
    if (!valid)
        valid = json_field(stock, unit, &rows) && *rows.start == '[';
    unsigned total = 0, n = 0;
    const char *next = valid ? rows.start + 1 : NULL;
    json_span_t row;
    while (valid && json_next(&next, rows.end, &row))
        total++;
    next = valid ? rows.start + 1 : NULL;
    unsigned index = 0;
    while (valid && json_next(&next, rows.end, &row)) {
        if (++index <= (total > STOCK_BARS ? total - STOCK_BARS : 0))
            continue;
        if (*row.start != '[') {
            valid = false;
            break;
        }
        char fields[6][32];
        const char *field_at = row.start + 1;
        for (unsigned j = 0; j < 6; j++) {
            json_span_t value;
            if (!json_next(&field_at, row.end, &value) ||
                !json_string(value, fields[j], sizeof(fields[j]))) {
                valid = false;
                break;
            }
        }
        if (!valid ||
            !stock_parse_bar(fields[0], fields[1], fields[2], fields[3], fields[4], &bars[n]) ||
            !stock_volume(fields[5], &bars[n].volume) ||
            (n && strcmp(bars[n - 1].date, bars[n].date) >= 0)) {
            valid = false;
            break;
        }
        n++;
    }
    if (!valid || !n) {
        *error = "K线数据不可用";
        return false;
    }
    *count = n;
    return true;
}
