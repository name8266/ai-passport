#pragma once
#include "esp_http_server.h"
#include "stock_core.h"
typedef struct {
    stock_config_t config;
    stock_quote_t quotes[STOCK_MAX];
    char status[96];
    bool stale;
} stock_web_state_t;
typedef bool (*stock_snapshot_fn)(stock_web_state_t *out);
typedef bool (*stock_config_fn)(const stock_config_t *config);
void stock_web_callbacks(stock_snapshot_fn snapshot, stock_config_fn update);
esp_err_t stock_web_page(httpd_req_t *req);
esp_err_t stock_web_state(httpd_req_t *req);
esp_err_t stock_web_config(httpd_req_t *req);
