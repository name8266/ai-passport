#include "cJSON.h"
#include "stock_web.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static stock_config_t received;
static bool accept = true;
static unsigned writes;
static bool snapshot(stock_web_state_t *s) {
    memset(s, 0, sizeof(*s));
    s->config = received;
    strcpy(s->status, "cached");
    s->stale = true;
    return true;
}
static bool update(const stock_config_t *c) {
    if (!accept)
        return false;
    received = *c;
    writes++;
    return true;
}
esp_err_t httpd_resp_set_type(httpd_req_t *r, const char *t) {
    (void)r;
    (void)t;
    return ESP_OK;
}
esp_err_t httpd_resp_set_hdr(httpd_req_t *r, const char *k, const char *v) {
    (void)r;
    (void)k;
    (void)v;
    return ESP_OK;
}
esp_err_t httpd_resp_set_status(httpd_req_t *r, const char *s) {
    r->status = atoi(s);
    return ESP_OK;
}
esp_err_t httpd_resp_send(httpd_req_t *r, const char *s, int n) {
    if (n < 0)
        n = (int)strlen(s);
    assert((unsigned)n < sizeof(r->response));
    memcpy(r->response, s, (size_t)n);
    r->response[n] = 0;
    return ESP_OK;
}
esp_err_t httpd_resp_sendstr(httpd_req_t *r, const char *s) { return httpd_resp_send(r, s, -1); }
esp_err_t httpd_resp_send_err(httpd_req_t *r, int error, const char *s) {
    r->status = error;
    return httpd_resp_sendstr(r, s);
}
int httpd_req_recv(httpd_req_t *r, char *s, size_t n) {
    size_t left = strlen(r->body) - r->read;
    if (n > left)
        n = left;
    if (n > 17)
        n = 17;
    memcpy(s, r->body + r->read, n);
    r->read += n;
    return (int)n;
}
static int post(const char *body) {
    httpd_req_t r = {.body = body, .content_len = (int)strlen(body), .status = 200};
    stock_web_config(&r);
    return r.status;
}
int main(void) {
    stock_web_callbacks(snapshot, update);
    const char *good =
        "{\"codes\":[\"000001\",\"600519\"],\"alerts\":[{\"code\":\"000001\",\"kind\":1,\"target\":"
        "12000}],\"sort\":2,\"power_save\":true,\"sound\":false}";
    assert(post(good) == 202 && writes == 1);
    assert(received.watch.count == 2 && received.alerts[0].target == 12000 &&
           received.prefs.sound == 0);
    assert(post("{}") == 400);
    assert(post("{\"codes\":[\"000001\",\"000001\"],\"alerts\":[],\"sort\":0,\"power_save\":true,"
                "\"sound\":true}") == 400);
    assert(post("{\"codes\":[\"000001\"],\"alerts\":[{\"code\":\"600519\",\"kind\":1,\"target\":"
                "1000}],\"sort\":0,\"power_save\":true,\"sound\":true}") == 400);
    assert(post("{\"codes\":[\"000001\"],\"alerts\":[{\"code\":\"000001\",\"kind\":3,\"target\":"
                "10001}],\"sort\":0,\"power_save\":true,\"sound\":true}") == 400);
    assert(post("{\"codes\":[\"000001\"],\"alerts\":[],\"sort\":0.5,\"power_save\":true,\"sound\":"
                "true}") == 400);
    assert(writes == 1);
    accept = false;
    assert(post(good) == 503 && writes == 1);
    httpd_req_t r = {0};
    assert(stock_web_state(&r) == ESP_OK);
    cJSON *root = cJSON_Parse(r.response);
    assert(cJSON_GetArraySize(cJSON_GetObjectItem(root, "stocks")) == 2);
    assert(cJSON_IsTrue(cJSON_GetObjectItem(root, "stale")));
    assert(cJSON_GetObjectItem(root, "password") == NULL);
    cJSON_Delete(root);
    puts("Stock web: PASS (configuration validation, partial reads, queue failure, state JSON, "
         "credential exclusion)");
    return 0;
}
