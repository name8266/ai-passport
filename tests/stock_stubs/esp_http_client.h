#pragma once
#include "esp_err.h"
#include <stdbool.h>
typedef struct client *esp_http_client_handle_t;
typedef struct {
    const char *url;
    int timeout_ms, buffer_size, buffer_size_tx;
    esp_err_t (*crt_bundle_attach)(void *);
    const char *user_agent;
} esp_http_client_config_t;
esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t *cfg);
esp_err_t esp_http_client_set_header(esp_http_client_handle_t c, const char *key,
                                     const char *value);
esp_err_t esp_http_client_open(esp_http_client_handle_t c, int bytes);
long long esp_http_client_fetch_headers(esp_http_client_handle_t c);
int esp_http_client_get_status_code(esp_http_client_handle_t c);
int esp_http_client_read(esp_http_client_handle_t c, char *data, int bytes);
bool esp_http_client_is_complete_data_received(esp_http_client_handle_t c);
esp_err_t esp_http_client_close(esp_http_client_handle_t c);
esp_err_t esp_http_client_cleanup(esp_http_client_handle_t c);
