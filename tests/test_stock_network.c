#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "stock_network.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
struct client {
    const char *body;
    size_t at;
};
static struct client client;
static char quote[1800], response[19000], last_url[256];
static bool complete = true;
static size_t read_chunk = 18000;
static int response_status = 200;
esp_err_t esp_crt_bundle_attach(void *c) {
    (void)c;
    return ESP_OK;
}
esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t *cfg) {
    assert(cfg->crt_bundle_attach == esp_crt_bundle_attach);
    assert(cfg->timeout_ms == 6000);
    snprintf(last_url, sizeof(last_url), "%s", cfg->url);
    client = (struct client){.body = strstr(cfg->url, "qt.gtimg.cn") ? quote : response};
    return &client;
}
esp_err_t esp_http_client_set_header(esp_http_client_handle_t c, const char *k, const char *v) {
    (void)c;
    assert(!strcmp(k, "Referer") && !strcmp(v, "https://gu.qq.com/"));
    return ESP_OK;
}
esp_err_t esp_http_client_open(esp_http_client_handle_t c, int n) {
    (void)c;
    (void)n;
    return ESP_OK;
}
long long esp_http_client_fetch_headers(esp_http_client_handle_t c) {
    return (long long)strlen(c->body);
}
int esp_http_client_get_status_code(esp_http_client_handle_t c) {
    (void)c;
    return response_status;
}
int esp_http_client_read(esp_http_client_handle_t c, char *data, int n) {
    size_t left = strlen(c->body) - c->at;
    if (left > read_chunk)
        left = read_chunk;
    if (left > (size_t)n)
        left = (size_t)n;
    memcpy(data, c->body + c->at, left);
    c->at += left;
    return (int)left;
}
bool esp_http_client_is_complete_data_received(esp_http_client_handle_t c) {
    return complete && c->at == strlen(c->body);
}
esp_err_t esp_http_client_close(esp_http_client_handle_t c) {
    (void)c;
    return ESP_OK;
}
esp_err_t esp_http_client_cleanup(esp_http_client_handle_t c) {
    (void)c;
    return ESP_OK;
}
static bool detail(stock_period_t period, unsigned *count, unsigned *minute_count,
                   stock_bar_t *bars, stock_minute_t *minutes) {
    stock_quote_t q = {0};
    char date[11] = {0};
    const char *error = NULL;
    *count = *minute_count = 0;
    return stock_fetch_detail("000001", period, &q, bars, count, minutes, minute_count, date,
                              &error);
}
int main(void) {
    strcpy(quote, "v_sz000001=\"51~Test~000001~11.57~11.35");
    for (int i = 5; i < 30; i++)
        strcat(quote, "~");
    strcat(quote, "~20260930161500~0.22~1.94\";");
    stock_bar_t bars[STOCK_BARS];
    stock_minute_t minutes[STOCK_MINUTES];
    unsigned count, n;
    const char *units[] = {"day", "week", "month"};
    for (unsigned i = 0; i < 3; i++) {
        snprintf(response, sizeof(response),
                 "{\"code\":0,\"data\":{\"sz000001\":{\"qfq%s\":[[\"2026-09-29\",\"11\",\"11.1\","
                 "\"11.5\",\"10.9\",\"12345.6\"],[\"2026-09-30\",\"11.1\",\"11.57\",\"11.6\","
                 "\"11\",\"23456\"]]}}}",
                 units[i]);
        assert(detail((stock_period_t)(i + 1), &count, &n, bars, minutes) && count == 2 && n == 0);
        assert(strstr(last_url, units[i]));
        assert(bars[0].volume == 12345);
    }
    strcpy(response,
           "{\"code\":0,\"data\":{\"sz000001\":{\"data\":{\"date\":\"20260930\",\"data\":[\"0929 "
           "11.5 0\",\"0930 11.5 100\",\"1130 11.6 200\",\"1200 11.6 200\",\"1300 11.5 "
           "300\",\"1500 11.57 400\",\"1501 11.57 401\",\"1530 11.57 450\"]}}}}");
    assert(detail(STOCK_INTRADAY, &count, &n, bars, minutes) && n == 4 && count == 0);
    assert(minutes[0].hhmm == 930 && minutes[3].hhmm == 1500 && minutes[3].volume == 400);
    char *date = strstr(response, "20260930");
    memcpy(date, "20260929", 8);
    assert(!detail(STOCK_INTRADAY, &count, &n, bars, minutes));
    memcpy(date, "20260930", 8);
    char *volume = strstr(response, "1300 11.5 300");
    memcpy(volume, "1300 11.5 100", 13);
    assert(!detail(STOCK_INTRADAY, &count, &n, bars, minutes));
    strcpy(response, "{\"code\":0,\"data\":{\"sz000001\":{\"qfqday\":[[\"2026-09-30\",\"11\","
                     "\"11\",\"12\",\"10\",\"-1\"]]}}}");
    assert(!detail(STOCK_DAY, &count, &n, bars, minutes));
    strcpy(response, "{\"code\":0,\"data\":{\"sz000001\":{\"day\":[[\"2026-09-30\",\"11\",\"11\","
                     "\"12\",\"10\",\"0\"]]}}}");
    assert(detail(STOCK_DAY, &count, &n, bars, minutes));
    complete = false;
    assert(!detail(STOCK_DAY, &count, &n, bars, minutes));
    complete = true;
    response_status = 503;
    assert(!detail(STOCK_DAY, &count, &n, bars, minutes));
    response_status = 200;
    memset(response, 'x', 18500);
    response[18500] = 0;
    assert(!detail(STOCK_DAY, &count, &n, bars, minutes));
    // A full 60-bar chart with metadata is parsed repeatedly in tiny HTTP chunks.
    read_chunk = 17;
    strcpy(response, "{\"code\":0,\"data\":{\"sz000001\":{\"qfqday\":[");
    for (unsigned i = 0; i < 60; i++) {
        char row[160];
        snprintf(row, sizeof(row),
                 "%s[\"2026-%02u-%02u\",\"11\",\"11.57\",\"12\",\"10\",\"23456\"]", i ? "," : "",
                 i / 28 + 1, i % 28 + 1);
        strcat(response, row);
    }
    strcat(response,
           "],\"qt\":{\"name\":\"\\u5e73\\u5b89\",\"other\":[null,true,false,-1.25e+3]}}}}");
    for (unsigned i = 0; i < 200; i++) {
        assert(detail(STOCK_DAY, &count, &n, bars, minutes) && count == 60);
        assert(bars[59].close == 11570);
    }
    char valid_body[19000];
    strcpy(valid_body, response);
    // Every truncated JSON must fail cleanly, including cuts inside escaped strings.
    for (size_t i = 0; i < strlen(valid_body); i++) {
        memcpy(response, valid_body, i);
        response[i] = 0;
        assert(!detail(STOCK_DAY, &count, &n, bars, minutes));
    }
    strcpy(response, valid_body);
    strcat(response, "garbage");
    assert(!detail(STOCK_DAY, &count, &n, bars, minutes));
    strcpy(response, "{\"code\":0,\"data\":{\"sz000001\":{\"qfqday\":[]}},\"bad\":");
    for (unsigned i = 0; i < 40; i++)
        strcat(response, "[");
    strcat(response, "0");
    for (unsigned i = 0; i < 40; i++)
        strcat(response, "]");
    strcat(response, "}");
    assert(!detail(STOCK_DAY, &count, &n, bars, minutes));
    puts(
        "Stock network: PASS (bounded allocation-free JSON, 60 bars x 200 requests, chunked reads, "
        "all JSON truncations, depth limit, periods, sessions, volume, dates, TLS)");
    return 0;
}
