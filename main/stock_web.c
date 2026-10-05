#include "stock_web.h"
#include "cJSON.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
static stock_snapshot_fn snapshot_fn;
static stock_config_fn update_fn;
extern const char html_start[] __asm__("_binary_stock_manager_html_start");
extern const char html_end[] __asm__("_binary_stock_manager_html_end");
void stock_web_callbacks(stock_snapshot_fn s, stock_config_fn u) {
    snapshot_fn = s;
    update_fn = u;
}
esp_err_t stock_web_page(httpd_req_t *r) {
    httpd_resp_set_type(r, "text/html; charset=utf-8");
    httpd_resp_set_hdr(r, "Cache-Control", "no-store");
    return httpd_resp_send(r, html_start, html_end - html_start - 1);
}
static esp_err_t unavailable(httpd_req_t *r, const char *message) {
    httpd_resp_set_status(r, "503 Service Unavailable");
    return httpd_resp_sendstr(r, message);
}
esp_err_t stock_web_state(httpd_req_t *r) {
    stock_web_state_t state;
    if (!snapshot_fn || !snapshot_fn(&state))
        return unavailable(r, "设备忙，请重试");
    cJSON *root = cJSON_CreateObject(), *stocks = cJSON_AddArrayToObject(root, "stocks"),
          *alerts = cJSON_AddArrayToObject(root, "alerts");
    if (!root || !stocks || !alerts) {
        cJSON_Delete(root);
        return httpd_resp_send_err(r, HTTPD_500_INTERNAL_SERVER_ERROR, "内存不足");
    }
    cJSON_AddStringToObject(root, "status", state.status);
    cJSON_AddBoolToObject(root, "stale", state.stale);
    cJSON_AddNumberToObject(root, "sort", state.config.prefs.sort);
    cJSON_AddBoolToObject(root, "power_save", state.config.prefs.power_save);
    cJSON_AddBoolToObject(root, "sound", state.config.prefs.sound);
    for (unsigned i = 0; i < state.config.watch.count; i++) {
        const stock_quote_t *q = &state.quotes[i];
        cJSON *s = cJSON_CreateObject();
        cJSON_AddStringToObject(s, "code", state.config.watch.codes[i]);
        cJSON_AddStringToObject(s, "name", q->valid ? q->name : "");
        cJSON_AddBoolToObject(s, "valid", q->valid);
        cJSON_AddNumberToObject(s, "price", q->price / 1000.0);
        cJSON_AddNumberToObject(s, "change", q->change_bp / 100.0);
        cJSON_AddStringToObject(s, "time", q->stamp);
        cJSON_AddItemToArray(stocks, s);
    }
    for (unsigned i = 0; i < STOCK_MAX; i++) {
        const stock_alert_t *a = &state.config.alerts[i];
        if (!a->kind)
            continue;
        cJSON *s = cJSON_CreateObject();
        cJSON_AddStringToObject(s, "code", a->code);
        cJSON_AddNumberToObject(s, "kind", a->kind);
        cJSON_AddNumberToObject(s, "target", a->target);
        cJSON_AddNumberToObject(s, "fired_date", a->fired_date);
        cJSON_AddItemToArray(alerts, s);
    }
    char *text = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!text)
        return httpd_resp_send_err(r, HTTPD_500_INTERNAL_SERVER_ERROR, "内存不足");
    httpd_resp_set_type(r, "application/json");
    httpd_resp_set_hdr(r, "Cache-Control", "no-store");
    esp_err_t err = httpd_resp_sendstr(r, text);
    free(text);
    return err;
}
static bool integer(cJSON *v, int min, int max) {
    return cJSON_IsNumber(v) && isfinite(v->valuedouble) && v->valuedouble >= min &&
           v->valuedouble <= max && v->valuedouble == v->valueint;
}
esp_err_t stock_web_config(httpd_req_t *r) {
    if (r->content_len < 2 || r->content_len > 3072)
        return httpd_resp_send_err(r, HTTPD_400_BAD_REQUEST, "请求过大");
    char *body = malloc(r->content_len + 1);
    if (!body)
        return ESP_ERR_NO_MEM;
    size_t read = 0;
    while (read < (size_t)r->content_len) {
        int n = httpd_req_recv(r, body + read, r->content_len - read);
        if (n <= 0) {
            free(body);
            return ESP_FAIL;
        }
        read += (size_t)n;
    }
    body[read] = 0;
    cJSON *root = cJSON_Parse(body);
    free(body);
    cJSON *codes = cJSON_GetObjectItemCaseSensitive(root, "codes"),
          *alerts = cJSON_GetObjectItemCaseSensitive(root, "alerts"),
          *sort = cJSON_GetObjectItemCaseSensitive(root, "sort"),
          *saving = cJSON_GetObjectItemCaseSensitive(root, "power_save"),
          *sound = cJSON_GetObjectItemCaseSensitive(root, "sound");
    stock_config_t next = {.prefs = {.version = 1}};
    bool valid = cJSON_IsArray(codes) && cJSON_GetArraySize(codes) <= STOCK_MAX &&
                 cJSON_IsArray(alerts) && cJSON_GetArraySize(alerts) <= STOCK_MAX &&
                 integer(sort, 0, 2) && cJSON_IsBool(saving) && cJSON_IsBool(sound);
    for (int i = 0; valid && i < cJSON_GetArraySize(codes); i++) {
        cJSON *code = cJSON_GetArrayItem(codes, i);
        valid = cJSON_IsString(code) && stock_watch_add(&next.watch, code->valuestring);
    }
    for (int i = 0; valid && i < cJSON_GetArraySize(alerts); i++) {
        cJSON *item = cJSON_GetArrayItem(alerts, i),
              *code = cJSON_GetObjectItemCaseSensitive(item, "code"),
              *kind = cJSON_GetObjectItemCaseSensitive(item, "kind"),
              *target = cJSON_GetObjectItemCaseSensitive(item, "target");
        valid = cJSON_IsString(code) && strlen(code->valuestring) == 6 && integer(kind, 1, 4) &&
                integer(target, 1, 9999990);
        if (!valid)
            break;
        stock_alert_t *a = &next.alerts[i];
        memcpy(a->code, code->valuestring, 7);
        a->kind = (uint8_t)kind->valueint;
        a->target = target->valueint;
        valid = stock_alert_valid(a) && stock_watch_find(&next.watch, a->code) >= 0;
        for (int j = 0; valid && j < i; j++)
            valid = strcmp(next.alerts[j].code, a->code) != 0;
    }
    if (valid) {
        next.prefs.sort = (uint8_t)sort->valueint;
        next.prefs.power_save = cJSON_IsTrue(saving);
        next.prefs.sound = cJSON_IsTrue(sound);
    }
    cJSON_Delete(root);
    if (!valid)
        return httpd_resp_send_err(r, HTTPD_400_BAD_REQUEST, "代码、预警或设置无效");
    if (!update_fn || !update_fn(&next))
        return unavailable(r, "队列忙，请重试");
    httpd_resp_set_status(r, "202 Accepted");
    return httpd_resp_sendstr(r, "已提交到设备，稍后自动刷新。若保存失败，页面状态会显示原因。");
}
