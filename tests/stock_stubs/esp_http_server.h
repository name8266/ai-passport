#pragma once
#include "esp_err.h"
#include <stddef.h>
typedef struct {
    int content_len;
    const char *body;
    size_t read;
    int status;
    char response[8192];
} httpd_req_t;
#define HTTPD_500_INTERNAL_SERVER_ERROR 500
#define HTTPD_400_BAD_REQUEST 400
esp_err_t httpd_resp_set_type(httpd_req_t *r, const char *type);
esp_err_t httpd_resp_set_hdr(httpd_req_t *r, const char *key, const char *value);
esp_err_t httpd_resp_set_status(httpd_req_t *r, const char *status);
esp_err_t httpd_resp_send(httpd_req_t *r, const char *data, int len);
esp_err_t httpd_resp_sendstr(httpd_req_t *r, const char *data);
esp_err_t httpd_resp_send_err(httpd_req_t *r, int error, const char *msg);
int httpd_req_recv(httpd_req_t *r, char *data, size_t len);
