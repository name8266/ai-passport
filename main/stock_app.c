#include "bsp_battery.h"
#include "bsp_button.h"
#include "bsp_display.h"
#include "cJSON.h"
#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "lvgl.h"
#include "nvs_flash.h"
#include "stock_core.h"
#include "stock_network.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
LV_FONT_DECLARE(stock_font_16);
#define READY BIT0
#define SETUP BIT1
#define LINK_LOST BIT2
#define INK 0xf1f3f5
#define MUTED 0x8b9aaa
#define UP_COLOR 0xff5967
#define DOWN_COLOR 0x3ed6a0
#define ACCENT 0x79baff
typedef enum { LIST, WHEEL, DETAIL, NETWORK } page_t;
typedef struct {
    bsp_btn_t key;
    bsp_btn_ev_t event;
} stock_key_t;
typedef struct {
    uint32_t generation;
    bool detail;
    stock_watch_t watch;
    char code[7];
} job_t;
typedef struct {
    uint32_t generation;
    bool detail, ok;
    char code[7];
    stock_quote_t quotes[STOCK_MAX];
    stock_quote_t quote;
    stock_bar_t bars[STOCK_BARS];
    unsigned count;
    const char *error;
} result_t;
static QueueHandle_t keys, jobs, results;
static EventGroupHandle_t wifi;
static nvs_handle_t settings, credentials;
static httpd_handle_t server;
static stock_watch_t watch;
static stock_quote_t quotes[STOCK_MAX], detail_quote;
static stock_bar_t bars[STOCK_BARS];
static unsigned bar_count, cursor, selected;
static stock_wheel_t wheel;
static page_t page = LIST;
static uint32_t generation;
static bool busy, stale = true;
static int battery = -1;
static int64_t next_refresh;
static char status[96] = "正在连接网络", ap_name[24], detail_code[7];
static lv_obj_t *screen, *title, *battery_label, *body, *help, *status_label, *price_label,
    *meta_label, *chart, *row_label[5], *row_box[5], *digit_label[6], *digit_box[6];
static void request(bool detail);
static void draw(void);
static void money(char *out, size_t cap, int32_t value) {
    snprintf(out, cap, "%.2f", value / 1000.0);
}
static void visible(lv_obj_t *o, bool yes) {
    if (yes)
        lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN);
    else
        lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
}
static lv_obj_t *label(int x, int y, int w, uint32_t color) {
    lv_obj_t *o = lv_label_create(screen);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_width(o, w);
    lv_obj_set_style_text_color(o, lv_color_hex(color), 0);
    lv_label_set_long_mode(o, LV_LABEL_LONG_DOT);
    return o;
}
static lv_obj_t *box(int x, int y, int w, int h) {
    lv_obj_t *o = lv_obj_create(screen);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_pad_all(o, 0, 0);
    lv_obj_set_style_border_width(o, 0, 0);
    lv_obj_set_style_radius(o, 8, 0);
    lv_obj_set_style_bg_color(o, lv_color_hex(0x18212d), 0);
    return o;
}
static int price_y(int32_t value, int32_t low, int32_t high, int top, int height) {
    return top + height - (int)(((int64_t)value - low) * height / (high - low));
}
static void chart_draw(lv_event_t *event) {
    if (!bar_count)
        return;
    lv_layer_t *layer = lv_event_get_layer(event);
    lv_area_t area;
    lv_obj_get_coords(chart, &area);
    int left = area.x1 + 5, top = area.y1 + 6, width = lv_area_get_width(&area) - 10,
        height = lv_area_get_height(&area) - 12;
    int32_t low = bars[0].low, high = bars[0].high;
    for (unsigned i = 1; i < bar_count; i++) {
        if (bars[i].low < low)
            low = bars[i].low;
        if (bars[i].high > high)
            high = bars[i].high;
    }
    if (high == low)
        high = low + 1;
    lv_draw_line_dsc_t line;
    lv_draw_line_dsc_init(&line);
    line.width = 1;
    line.color = lv_color_hex(0x283544);
    for (int i = 0; i < 3; i++) {
        line.p1 = (lv_point_precise_t){left, top + i * height / 2};
        line.p2 = (lv_point_precise_t){left + width, top + i * height / 2};
        lv_draw_line(layer, &line);
    }
    for (unsigned i = 0; i < bar_count; i++) {
        stock_bar_t *b = &bars[i];
        int x = left + (int)((i * 2 + 1) * width / (bar_count * 2));
        uint32_t color = b->close >= b->open ? UP_COLOR : DOWN_COLOR;
        line.color = lv_color_hex(color);
        line.p1 = (lv_point_precise_t){x, price_y(b->high, low, high, top, height)};
        line.p2 = (lv_point_precise_t){x, price_y(b->low, low, high, top, height)};
        lv_draw_line(layer, &line);
        int y1 = price_y(b->open, low, high, top, height),
            y2 = price_y(b->close, low, high, top, height);
        if (y1 > y2) {
            int t = y1;
            y1 = y2;
            y2 = t;
        }
        int half = width / (int)bar_count / 2;
        if (half < 1)
            half = 1;
        lv_area_t rect = {x - half, y1, x + half - 1, y2};
        lv_draw_rect_dsc_t d;
        lv_draw_rect_dsc_init(&d);
        d.bg_color = lv_color_hex(color);
        d.bg_opa = LV_OPA_COVER;
        lv_draw_rect(layer, &d, &rect);
    }
    unsigned active = cursor < bar_count ? cursor : bar_count - 1;
    int x = left + (int)((active * 2 + 1) * width / (bar_count * 2));
    line.color = lv_color_hex(ACCENT);
    line.dash_width = 2;
    line.dash_gap = 3;
    line.p1 = (lv_point_precise_t){x, top};
    line.p2 = (lv_point_precise_t){x, top + height};
    lv_draw_line(layer, &line);
}
static void draw(void) {
    if (!bsp_lvgl_lock(-1))
        return;
    lv_label_set_text(title, page == LIST     ? "自选行情"
                             : page == WHEEL  ? "选择股票代码"
                             : page == DETAIL ? "日K · 前复权"
                                              : "无线网络设置");
    if (battery >= 0)
        lv_label_set_text_fmt(battery_label, "%d%%", battery);
    else
        lv_label_set_text(battery_label, "--");
    lv_label_set_text(status_label, status);
    lv_obj_set_style_text_color(status_label, lv_color_hex(stale ? 0xf3bc6a : MUTED), 0);
    visible(body, page == WHEEL || page == NETWORK);
    visible(price_label, page == DETAIL);
    visible(meta_label, page == DETAIL);
    visible(chart, page == DETAIL && bar_count);
    for (int i = 0; i < 5; i++) {
        visible(row_box[i], page == LIST);
        visible(row_label[i], page == LIST);
    }
    for (int i = 0; i < 6; i++) {
        visible(digit_box[i], page == WHEEL);
        visible(digit_label[i], page == WHEEL);
    }
    if (page == LIST) {
        unsigned start = selected >= 4 ? selected - 3 : 0;
        for (unsigned row = 0; row < 5; row++) {
            unsigned i = start + row;
            visible(row_box[row], i <= watch.count);
            visible(row_label[row], i <= watch.count);
            if (i > watch.count)
                continue;
            lv_obj_set_style_bg_color(row_box[row],
                                      lv_color_hex(i == selected ? 0x233d58 : 0x18212d), 0);
            if (i == watch.count) {
                lv_label_set_text(row_label[row], "+ 添加自选 / 查询");
                lv_obj_set_style_text_color(row_label[row], lv_color_hex(ACCENT), 0);
            } else {
                char price[20];
                stock_quote_t *q = &quotes[i];
                money(price, sizeof(price), q->price);
                lv_label_set_text_fmt(row_label[row], "%s %s\n%s  %+.2f%%", watch.codes[i],
                                      q->valid ? q->name : "等待行情", q->valid ? price : "--",
                                      q->valid ? q->change_bp / 100.0 : 0.0);
                lv_obj_set_style_text_color(row_label[row],
                                            lv_color_hex(!q->valid           ? MUTED
                                                         : q->change_bp >= 0 ? UP_COLOR
                                                                             : DOWN_COLOR),
                                            0);
            }
        }
        lv_label_set_text(help, "UP/DOWN 选择 · OK 查看\n长按UP 刷新 · 长按OK 配网");
    } else if (page == WHEEL) {
        lv_obj_set_pos(body, 20, 166);
        for (int i = 0; i < 6; i++) {
            int n = wheel.code[i] - '0';
            lv_label_set_text_fmt(digit_label[i], "%d\n%d\n%d", (n + 1) % 10, n, (n + 9) % 10);
            lv_obj_set_style_bg_color(digit_box[i],
                                      lv_color_hex(i == wheel.digit ? 0x294c70 : 0x18212d), 0);
            lv_obj_set_style_text_color(digit_label[i],
                                        lv_color_hex(i == wheel.digit ? INK : MUTED), 0);
        }
        char symbol[9];
        stock_symbol(wheel.code, symbol);
        lv_label_set_text_fmt(body, "第 %u / 6 位\n%s\n最后一位按 OK 查询", wheel.digit + 1,
                              stock_symbol(wheel.code, symbol)
                                  ? (!strncmp(symbol, "sh", 2)   ? "上海 A 股"
                                     : !strncmp(symbol, "sz", 2) ? "深圳 A 股"
                                                                 : "北京 A 股")
                                  : "请输入有效代码");
        lv_label_set_text(help, "UP/DOWN 转动 · OK 下一位\n长按UP 上一位 · 长按OK 返回");
    } else if (page == DETAIL) {
        char price[20];
        money(price, sizeof(price), detail_quote.price);
        lv_label_set_text_fmt(price_label, "%s %s\n%s  %+.2f%%", detail_code,
                              detail_quote.valid ? detail_quote.name : "等待行情",
                              detail_quote.valid ? price : "--",
                              detail_quote.valid ? detail_quote.change_bp / 100.0 : 0.0);
        lv_obj_set_style_text_color(
            price_label, lv_color_hex(detail_quote.change_bp >= 0 ? UP_COLOR : DOWN_COLOR), 0);
        if (bar_count) {
            stock_bar_t *b = &bars[cursor];
            lv_label_set_text_fmt(meta_label, "%s  收 %.2f\n开 %.2f  高 %.2f  低 %.2f", b->date,
                                  b->close / 1000.0, b->open / 1000.0, b->high / 1000.0,
                                  b->low / 1000.0);
            lv_obj_invalidate(chart);
        } else
            lv_label_set_text(meta_label, "等待最近60根日K线");
        lv_label_set_text(help, stock_watch_find(&watch, detail_code) >= 0
                                    ? "UP/DOWN 看K线 · OK 刷新\n长按UP 删除自选 · 长按OK 返回"
                                    : "UP/DOWN 看K线 · OK 刷新\n长按UP 加入自选 · 长按OK 返回");
    } else {
        lv_obj_set_pos(body, 20, 58);
        lv_label_set_text_fmt(
            body, "连接开放热点\n%s\n\n浏览器打开\n192.168.4.1\n填写 2.4GHz Wi-Fi", ap_name);
        lv_label_set_text(help, "无热点密码 · 保存后自动连接\n长按OK 返回自选");
    }
    bsp_lvgl_unlock();
}
static void button(bsp_btn_t key, bsp_btn_ev_t event, void *arg) {
    (void)arg;
    if (event == BSP_BTN_CLICK || event == BSP_BTN_LONG) {
        stock_key_t input = {key, event};
        xQueueSend(keys, &input, 0);
    }
}
static void request(bool detail) {
    job_t job = {.generation = ++generation, .detail = detail, .watch = watch};
    memcpy(job.code, detail_code, 7);
    busy = true;
    strcpy(status, "正在查询行情…");
    xQueueOverwrite(jobs, &job);
    next_refresh = esp_timer_get_time() + 30000000LL;
}
static void worker(void *arg) {
    (void)arg;
    job_t job;
    for (;;) {
        xQueueReceive(jobs, &job, portMAX_DELAY);
        result_t result = {.generation = job.generation, .detail = job.detail};
        memcpy(result.code, job.code, 7);
        if (!(xEventGroupGetBits(wifi) & READY))
            result.error = "网络未连接 · 请配网";
        else if (time(NULL) < 1704067200 && esp_netif_sntp_sync_wait(pdMS_TO_TICKS(8000)) != ESP_OK)
            result.error = "网络校时失败 · OK重试";
        else if (job.detail)
            result.ok = stock_fetch_detail(job.code, &result.quote, result.bars, &result.count,
                                           &result.error);
        else
            result.ok = stock_fetch_quotes(&job.watch, result.quotes, &result.error);
        ESP_LOGI("stocks", "fetch %s generation=%lu success=%d bars=%u",
                 job.detail ? job.code : "watchlist", (unsigned long)job.generation, result.ok,
                 result.count);
        xQueueSend(results, &result, portMAX_DELAY);
    }
}
static void wifi_event(void *arg, esp_event_base_t base, int32_t id, void *data) {
    (void)arg;
    (void)data;
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START)
        esp_wifi_connect();
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        xEventGroupClearBits(wifi, READY);
        xEventGroupSetBits(wifi, LINK_LOST);
    }
    if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        esp_netif_sntp_start();
        xEventGroupSetBits(wifi, READY);
        xEventGroupClearBits(wifi, LINK_LOST);
    }
}
static esp_err_t setup_page(httpd_req_t *req) {
    const char *html =
        "<!doctype html><meta charset='utf-8'><meta name='viewport' "
        "content='width=device-width'><title>股票看板配网</title><style>body{font:18px "
        "system-ui;max-width:460px;margin:30px "
        "auto;padding:20px;background:#101720;color:white}input,button{box-sizing:border-box;width:"
        "100%;padding:14px;margin:12px 0}</style><h1>股票看板配网</h1><form><label>2.4GHz Wi-Fi "
        "名称<input name='ssid' maxlength='32' required></label><label>密码<input name='password' "
        "type='password' maxlength='63'></label><button>保存并连接</button></form><p "
        "id='status'></p><script>document.querySelector('form').onsubmit=async "
        "e=>{e.preventDefault();try{const r=await "
        "fetch('/setup',{method:'POST',headers:{'Content-Type':'application/"
        "json'},body:JSON.stringify(Object.fromEntries(new "
        "FormData(e.target)))});document.querySelector('#status').textContent=await "
        "r.text()}catch(e){document.querySelector('#status').textContent='请查看设备连接状态'}}</"
        "script>";
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    return httpd_resp_sendstr(req, html);
}
static esp_err_t save_setup(httpd_req_t *req) {
    if (req->content_len <= 0 || req->content_len >= 512)
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "配置长度无效");
    char data[512];
    int used = 0;
    while (used < req->content_len) {
        int n = httpd_req_recv(req, data + used, req->content_len - used);
        if (n <= 0)
            return ESP_FAIL;
        used += n;
    }
    data[used] = 0;
    cJSON *root = cJSON_Parse(data), *ssid = cJSON_GetObjectItemCaseSensitive(root, "ssid"),
          *pass = cJSON_GetObjectItemCaseSensitive(root, "password");
    if (!cJSON_IsString(ssid) || !cJSON_IsString(pass) || !ssid->valuestring[0] ||
        strlen(ssid->valuestring) > 32 || strlen(pass->valuestring) > 63 ||
        (strlen(pass->valuestring) > 0 && strlen(pass->valuestring) < 8)) {
        cJSON_Delete(root);
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "请检查名称和密码");
    }
    esp_err_t err = nvs_set_str(credentials, "ssid", ssid->valuestring);
    if (err == ESP_OK)
        err = nvs_set_str(credentials, "password", pass->valuestring);
    if (err == ESP_OK)
        err = nvs_commit(credentials);
    cJSON_Delete(root);
    if (err != ESP_OK)
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "保存失败");
    xEventGroupSetBits(wifi, SETUP);
    return httpd_resp_sendstr(req, "已保存，正在连接。请查看设备。");
}
static void start_setup(void) {
    wifi_config_t ap = {0};
    snprintf((char *)ap.ap.ssid, sizeof(ap.ap.ssid), "%s", ap_name);
    ap.ap.authmode = WIFI_AUTH_OPEN;
    ap.ap.max_connection = 1;
    ap.ap.channel = 1;
    esp_wifi_set_mode(WIFI_MODE_APSTA);
    esp_wifi_set_config(WIFI_IF_AP, &ap);
    if (!server) {
        httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
        cfg.max_uri_handlers = 2;
        cfg.stack_size = 6144;
        if (httpd_start(&server, &cfg) == ESP_OK) {
            httpd_uri_t get = {.uri = "/", .method = HTTP_GET, .handler = setup_page},
                        post = {.uri = "/setup", .method = HTTP_POST, .handler = save_setup};
            httpd_register_uri_handler(server, &get);
            httpd_register_uri_handler(server, &post);
        }
    }
    ++generation;
    busy = false;
    page = NETWORK;
    strcpy(status, server ? "配网热点无需密码" : "配网服务失败，请重启");
    draw();
}
static void connect_saved(void) {
    char ssid[33] = {0}, pass[64] = {0};
    size_t n = sizeof(ssid);
    nvs_get_str(credentials, "ssid", ssid, &n);
    n = sizeof(pass);
    nvs_get_str(credentials, "password", pass, &n);
    if (!ssid[0]) {
        start_setup();
        return;
    }
    wifi_config_t sta = {0};
    memcpy(sta.sta.ssid, ssid, strlen(ssid));
    memcpy(sta.sta.password, pass, strlen(pass));
    sta.sta.threshold.authmode = pass[0] ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;
    esp_wifi_disconnect();
    esp_wifi_set_config(WIFI_IF_STA, &sta);
    esp_wifi_connect();
}
static void select_detail(const char *code) {
    memcpy(detail_code, code, 7);
    detail_quote = (stock_quote_t){0};
    if (bsp_lvgl_lock(-1)) {
        bar_count = 0;
        cursor = 0;
        bsp_lvgl_unlock();
    }
    page = DETAIL;
    request(true);
}
static void handle(stock_key_t key) {
    if (key.event == BSP_BTN_LONG) {
        if (key.key == BSP_BTN_OK) {
            if (page == LIST) {
                start_setup();
                return;
            }
            ++generation;
            busy = false;
            page = LIST;
            selected = selected > watch.count ? watch.count : selected;
            request(false);
        } else if (key.key == BSP_BTN_UP) {
            if (page == WHEEL) {
                if (wheel.digit)
                    wheel.digit--;
            } else if (page == LIST)
                request(false);
            else if (page == DETAIL) {
                stock_watch_t next = watch;
                bool changed;
                if (stock_watch_find(&watch, detail_code) >= 0)
                    changed = stock_watch_remove(&next, detail_code);
                else if (!detail_quote.valid) {
                    strcpy(status, "有效行情返回后才能添加");
                    draw();
                    return;
                } else
                    changed = stock_watch_add(&next, detail_code);
                if (!changed)
                    strcpy(status, "自选已满，最多12只");
                else if (nvs_set_blob(settings, "watch_v1", &next, sizeof(next)) != ESP_OK ||
                         nvs_commit(settings) != ESP_OK)
                    strcpy(status, "保存失败，请重试");
                else {
                    watch = next;
                    memset(quotes, 0, sizeof(quotes));
                    strcpy(status, stock_watch_find(&watch, detail_code) >= 0 ? "已加入自选"
                                                                              : "已删除自选");
                }
            }
        }
    } else if (key.event == BSP_BTN_CLICK) {
        if (page == LIST) {
            if (key.key == BSP_BTN_UP)
                selected = selected ? selected - 1 : watch.count;
            else if (key.key == BSP_BTN_DOWN)
                selected = (selected + 1) % (watch.count + 1);
            else if (selected == watch.count) {
                ++generation;
                busy = false;
                stock_wheel_init(&wheel, "000001");
                page = WHEEL;
                strcpy(status, "输入六位A股代码");
            } else
                select_detail(watch.codes[selected]);
        } else if (page == WHEEL) {
            if (key.key == BSP_BTN_UP)
                stock_wheel_turn(&wheel, 1);
            else if (key.key == BSP_BTN_DOWN)
                stock_wheel_turn(&wheel, -1);
            else if (stock_wheel_next(&wheel)) {
                if (stock_symbol(wheel.code, NULL))
                    select_detail(wheel.code);
                else
                    strcpy(status, "代码无效，请调整数字");
            }
        } else if (page == DETAIL) {
            if (key.key == BSP_BTN_OK)
                request(true);
            else if (bsp_lvgl_lock(-1)) {
                if (bar_count) {
                    if (key.key == BSP_BTN_UP)
                        cursor = cursor ? cursor - 1 : 0;
                    else if (cursor + 1 < bar_count)
                        cursor++;
                }
                bsp_lvgl_unlock();
            }
        }
    }
    draw();
}
static void ui_task(void *arg) {
    (void)arg;
    int64_t next_battery = 0, retry = 0;
    bool was_ready = false;
    for (;;) {
        stock_key_t key;
        if (xQueueReceive(keys, &key, pdMS_TO_TICKS(40)))
            handle(key);
        result_t r;
        if (xQueueReceive(results, &r, 0) && r.generation == generation) {
            busy = false;
            stale = !r.ok;
            if (r.detail) {
                if (r.quote.valid)
                    detail_quote = r.quote;
                if (r.ok && bsp_lvgl_lock(-1)) {
                    memcpy(bars, r.bars, r.count * sizeof(*bars));
                    bar_count = r.count;
                    cursor = bar_count - 1;
                    bsp_lvgl_unlock();
                }
            } else
                for (unsigned i = 0; i < watch.count; i++)
                    if (r.quotes[i].valid && !strcmp(r.quotes[i].code, watch.codes[i]))
                        quotes[i] = r.quotes[i];
            const char *stamp =
                r.detail ? detail_quote.stamp : (watch.count ? quotes[0].stamp : "");
            if (!r.ok)
                snprintf(status, sizeof(status), "%s · 旧数据", r.error ? r.error : "查询失败");
            else if (stamp[0])
                snprintf(status, sizeof(status), "行情 %.4s-%.2s-%.2s %.2s:%.2s", stamp, stamp + 4,
                         stamp + 6, stamp + 8, stamp + 10);
            else
                strcpy(status, "暂无自选 · OK添加");
            draw();
        }
        EventBits_t bits = xEventGroupGetBits(wifi);
        bool ready = bits & READY;
        int64_t now = esp_timer_get_time();
        if (bits & SETUP) {
            xEventGroupClearBits(wifi, SETUP);
            connect_saved();
            strcpy(status, "正在连接网络");
            draw();
        }
        if (ready && !was_ready) {
            page = LIST;
            if (server) {
                httpd_stop(server);
                server = NULL;
            }
            esp_wifi_set_mode(WIFI_MODE_STA);
            request(false);
            draw();
        }
        if (!ready && was_ready) {
            stale = true;
            strcpy(status, "网络断开 · 显示旧数据");
            draw();
        }
        was_ready = ready;
        if (!ready && (bits & LINK_LOST) && now >= retry) {
            esp_wifi_connect();
            retry = now + 10000000LL;
        }
        if (ready && !busy && (page == LIST || page == DETAIL) && now >= next_refresh) {
            request(page == DETAIL);
            draw();
        }
        if (now >= next_battery) {
            battery = bsp_battery_soc();
            next_battery = now + 15000000LL;
            draw();
        }
    }
}
void app_main(void) {
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(nvs_open("stocks", NVS_READWRITE, &settings));
    ESP_ERROR_CHECK(nvs_open("worldcam", NVS_READWRITE, &credentials));
    stock_watch_t saved;
    size_t n = sizeof(saved);
    if (nvs_get_blob(settings, "watch_v1", &saved, &n) == ESP_OK &&
        stock_watch_decode(&watch, &saved, n)) {
    } else
        stock_watch_add(&watch, "000001");
    ESP_ERROR_CHECK(bsp_display_init());
    if (!bsp_lvgl_init())
        abort();
    bsp_display_backlight(100);
    bsp_battery_init();
    keys = xQueueCreate(12, sizeof(stock_key_t));
    jobs = xQueueCreate(1, sizeof(job_t));
    results = xQueueCreate(1, sizeof(result_t));
    wifi = xEventGroupCreate();
    if (!keys || !jobs || !results || !wifi)
        abort();
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    if (!esp_netif_create_default_wifi_sta() || !esp_netif_create_default_wifi_ap())
        abort();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    esp_wifi_set_storage(WIFI_STORAGE_RAM);
    uint8_t mac[6];
    esp_wifi_get_mac(WIFI_IF_STA, mac);
    snprintf(ap_name, sizeof(ap_name), "Stocks-%02X%02X", mac[4], mac[5]);
    esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event, NULL);
    esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event, NULL);
    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_sntp_config_t ntp = ESP_NETIF_SNTP_DEFAULT_CONFIG_MULTIPLE(
        2, ESP_SNTP_SERVER_LIST("ntp.aliyun.com", "ntp.tencent.com"));
    ntp.start = false;
    ESP_ERROR_CHECK(esp_netif_sntp_init(&ntp));
    ESP_ERROR_CHECK(esp_wifi_start());
    if (!bsp_lvgl_lock(-1))
        abort();
    screen = lv_obj_create(NULL);
    lv_obj_remove_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x101720), 0);
    lv_obj_set_style_text_font(screen, &stock_font_16, 0);
    lv_screen_load(screen);
    title = label(18, 18, 158, INK);
    battery_label = label(185, 18, 40, MUTED);
    for (int i = 0; i < 5; i++) {
        row_box[i] = box(14, 48 + i * 42, 212, 39);
        row_label[i] = label(22, 50 + i * 42, 198, INK);
    }
    body = label(20, 170, 200, INK);
    lv_label_set_long_mode(body, LV_LABEL_LONG_WRAP);
    price_label = label(18, 47, 208, UP_COLOR);
    meta_label = label(18, 208, 208, MUTED);
    lv_label_set_long_mode(meta_label, LV_LABEL_LONG_WRAP);
    chart = box(16, 96, 208, 108);
    lv_obj_add_event_cb(chart, chart_draw, LV_EVENT_DRAW_MAIN, NULL);
    for (int i = 0; i < 6; i++) {
        digit_box[i] = box(14 + i * 36, 70, 32, 88);
        digit_label[i] = label(22 + i * 36, 82, 20, INK);
        lv_label_set_long_mode(digit_label[i], LV_LABEL_LONG_WRAP);
    }
    status_label = label(18, 257, 208, MUTED);
    help = label(18, 279, 208, MUTED);
    lv_obj_set_style_text_font(help, &stock_font_16, 0);
    lv_label_set_long_mode(help, LV_LABEL_LONG_WRAP);
    lv_obj_set_pos(body, 20, 166);
    bsp_lvgl_unlock();
    ESP_ERROR_CHECK(bsp_button_init(button, NULL));
    connect_saved();
    draw();
    if (xTaskCreate(worker, "stocks_net", 16384, NULL, 4, NULL) != pdPASS ||
        xTaskCreate(ui_task, "stocks_ui", 8192, NULL, 5, NULL) != pdPASS)
        abort();
}
