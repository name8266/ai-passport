#include "stock_network.h"
#include "cJSON.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define RESPONSE_LIMIT 18000
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
    body = malloc(RESPONSE_LIMIT + 1);
    if (!body) {
        *error = "行情内存不足";
        goto done;
    }
    size_t used = 0;
    int n;
    while (used < RESPONSE_LIMIT &&
           (n = esp_http_client_read(c, body + used, RESPONSE_LIMIT - used)) > 0)
        used += (size_t)n;
    if (used == RESPONSE_LIMIT || !esp_http_client_is_complete_data_received(c)) {
        *error = "行情响应不完整";
        free(body);
        body = NULL;
        goto done;
    }
    body[used] = 0;
done:
    esp_http_client_close(c);
    esp_http_client_cleanup(c);
    return body;
}
bool stock_fetch_quotes(const stock_watch_t *w, stock_quote_t quotes[STOCK_MAX],
                        const char **error) {
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
    free(body);
    return all;
}
bool stock_fetch_detail(const char *code, stock_quote_t *q, stock_bar_t bars[STOCK_BARS],
                        unsigned *count, const char **error) {
    stock_watch_t one = {0};
    stock_watch_add(&one, code);
    stock_quote_t quote[STOCK_MAX] = {0};
    if (!stock_fetch_quotes(&one, quote, error))
        return false;
    *q = quote[0];
    char symbol[9], url[200];
    if (!stock_symbol(code, symbol)) {
        *error = "股票代码无效";
        return false;
    }
    snprintf(url, sizeof(url),
             "https://web.ifzq.gtimg.cn/appstock/app/fqkline/get?param=%s,day,,,60,qfq", symbol);
    char *body = get(url, error);
    if (!body)
        return false;
    cJSON *root = cJSON_Parse(body);
    free(body);
    cJSON *rc = cJSON_GetObjectItemCaseSensitive(root, "code");
    cJSON *data = cJSON_GetObjectItemCaseSensitive(root, "data");
    cJSON *stock = cJSON_GetObjectItemCaseSensitive(data, symbol);
    cJSON *rows = cJSON_GetObjectItemCaseSensitive(stock, "qfqday");
    if (!cJSON_IsArray(rows))
        rows = cJSON_GetObjectItemCaseSensitive(stock, "day");
    bool valid = cJSON_IsNumber(rc) && rc->valueint == 0 && cJSON_IsArray(rows);
    unsigned n = 0;
    if (valid) {
        int total = cJSON_GetArraySize(rows);
        int start = total > STOCK_BARS ? total - STOCK_BARS : 0;
        for (int i = start; i < total; i++) {
            cJSON *row = cJSON_GetArrayItem(rows, i);
            const char *fields[5];
            for (int j = 0; j < 5; j++) {
                cJSON *v = cJSON_GetArrayItem(row, j);
                fields[j] = cJSON_IsString(v) ? v->valuestring : NULL;
            }
            if (!stock_parse_bar(fields[0], fields[1], fields[2], fields[3], fields[4], &bars[n])) {
                valid = false;
                break;
            }
            if (n && strcmp(bars[n - 1].date, bars[n].date) >= 0) {
                valid = false;
                break;
            }
            n++;
        }
    }
    cJSON_Delete(root);
    if (!valid || !n) {
        *error = "K线数据不可用";
        return false;
    }
    *count = n;
    return true;
}
