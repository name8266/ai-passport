#include "bsp_battery.h"
#include "bsp_button.h"
#include "bsp_display.h"
#include "cJSON.h"
#include "esp_event.h"
#include "esp_heap_caps.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "lvgl.h"
#include "src/misc/cache/instance/lv_image_cache.h"
#include "nvs_flash.h"
#include "stock_core.h"
#include "stock_network.h"
#include "stock_preview.h"
#include "stock_sound.h"
#include "stock_web.h"
#include "stock_wifi.h"
#include <stdio.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
_Static_assert(LV_BIN_DECODER_RAM_LOAD == 0, "Preview images require line-by-line decoding");
LV_FONT_DECLARE(stock_font_16);
LV_FONT_DECLARE(stock_font_12);
#define READY BIT0
#define SETUP BIT1
#define LINK_LOST BIT2
#define INK 0xeaf3ff
#define MUTED 0x8397b3
#define UP_COLOR 0xff6575
#define DOWN_COLOR 0x44d8ad
#define ACCENT 0x79baff
#define GOLD 0xf5c76c
#define VIOLET 0xc397ff
typedef enum { LIST, WHEEL, DETAIL, NETWORK, MENU, ALERT } page_t;
typedef struct {
    bsp_btn_t key;
    bsp_btn_ev_t event;
} stock_key_t;
typedef struct {
    uint32_t generation;
    bool detail, preview;
    stock_quote_t quote;
    stock_period_t period;
    stock_watch_t watch;
    char code[7];
} job_t;
typedef struct {
    uint32_t generation;
    bool detail, preview, ok, quotes_ok;
    stock_period_t period;
    stock_quote_t quotes[STOCK_MAX], quote;
    union {
        stock_bar_t bars[STOCK_BARS];
        stock_minute_t minutes[STOCK_MINUTES];
    };
    unsigned count, minute_count;
    char minute_date[11];
    const char *error;
} result_t;
typedef struct {
    uint32_t version;
    stock_quote_t quotes[STOCK_MAX];
} cache_t;
static QueueHandle_t keys, jobs, results, commands, sounds;
static EventGroupHandle_t wifi;
static SemaphoreHandle_t state_lock, result_consumed;
static result_t network_result;
static nvs_handle_t settings, credentials;
static stock_wifi_source_t active_wifi_source;
static unsigned wifi_failed_attempts;
static httpd_handle_t server;
static stock_watch_t watch;
static stock_preferences_t prefs = {.version = 2, .sort = 0, .power_save = 1, .sound = 1};
static stock_alert_t alerts[STOCK_MAX];
static stock_quote_t quotes[STOCK_MAX], detail_quote;
static stock_bar_t bars[STOCK_BARS];
static stock_minute_t minutes[STOCK_MINUTES];
static stock_preview_t previews[STOCK_MAX];
static int64_t preview_attempt[STOCK_MAX];
static uint32_t preview_version[STOCK_MAX];
static uint8_t preview_state[STOCK_MAX];
static unsigned list_first;
static page_t drawn_page = NETWORK;
/* Fixed I1 rasters: 608 bytes/card, no per-segment LVGL draw allocations. */
static _Alignas(4) uint8_t preview_pixels[STOCK_LIST_SLOTS][8 + STOCK_SPARK_BYTES];
static lv_image_dsc_t preview_images[STOCK_LIST_SLOTS];
static lv_obj_t *list_view, *list_extent;
static bool row_preview_show[STOCK_LIST_SLOTS], row_preview_dim[STOCK_LIST_SLOTS];
static bool row_selected[STOCK_LIST_SLOTS];
static uint32_t row_curve_color[STOCK_LIST_SLOTS];
static bool list_chrome_visible, list_cached, list_force_snap = true;
static unsigned list_display_count;
static atomic_bool list_scroll_finished;
static char minute_date[11], alert_banner[96], detail_code[7], ap_name[24],
    status[96] = "正在连接网络";
static unsigned bar_count, minute_count, cursor, selected, menu_selected;
static uint8_t order[STOCK_MAX];
static stock_wheel_t wheel;
static uint8_t edit_alert_kind = STOCK_PRICE_ABOVE;
static stock_period_t period = STOCK_DAY;
static bool indicators = true, busy, stale = true, dimmed;
static page_t page = LIST, menu_parent = LIST;
static bool adding_from_list, added_hint;
static uint32_t generation;
static int battery = -1;
static int64_t next_refresh, last_input, cache_saved, alert_until;
static uint16_t cache_valid_mask, fresh_mask;
static bool detail_fresh;
static lv_obj_t *screen, *title, *battery_label, *subtitle, *status_label, *help, *body, *chart,
    *name_label, *price_label, *change_label, *meta_label, *tabs[4], *row_box[STOCK_LIST_SLOTS], *row_name[STOCK_LIST_SLOTS],
    *row_price[STOCK_LIST_SLOTS], *row_code[STOCK_LIST_SLOTS], *row_change[STOCK_LIST_SLOTS], *digit_box[6], *digit_label[6], *hero, *toast,
    *toast_label;
static void draw(void);
static void request(bool detail);
static void start_setup(void);
static esp_err_t save_setup(httpd_req_t *req);
static void connect_saved(void);
static bool has_alerts(void) {
    for (unsigned i = 0; i < STOCK_MAX; i++)
        if (alerts[i].kind)
            return true;
    return false;
}
static stock_config_t configuration(void) {
    stock_config_t c = {.watch = watch, .prefs = prefs};
    memcpy(c.alerts, alerts, sizeof(alerts));
    return c;
}
static bool persist(const stock_config_t *c) {
    return stock_config_valid(c) && nvs_set_blob(settings, "board_v2", c, sizeof(*c)) == ESP_OK &&
           nvs_commit(settings) == ESP_OK;
}
static void align_quotes(const stock_quote_t old[STOCK_MAX]) {
    uint16_t old_fresh = fresh_mask, aligned_fresh = 0;
    memset(previews, 0, sizeof(previews));
    memset(preview_attempt, 0, sizeof(preview_attempt));
    memset(preview_state, 0, sizeof(preview_state));
    for (unsigned i = 0; i < STOCK_MAX; i++)
        preview_version[i]++;
    list_first = 0;
    list_force_snap = true;
    memset(quotes, 0, sizeof(quotes));
    for (unsigned i = 0; i < watch.count; i++)
        for (unsigned j = 0; j < STOCK_MAX; j++)
            if (old[j].valid && !strcmp(old[j].code, watch.codes[i])) {
                quotes[i] = old[j];
                if (old_fresh & (1u << j))
                    aligned_fresh |= (uint16_t)(1u << i);
                break;
            }
    fresh_mask = aligned_fresh;
}
static void save_cache(bool force) {
    int64_t now = esp_timer_get_time();
    uint16_t valid_mask = 0;
    for (unsigned i = 0; i < watch.count; i++)
        if (quotes[i].valid)
            valid_mask |= (uint16_t)(1u << i);
    if (!force && now - cache_saved < 300000000LL && !(valid_mask & ~cache_valid_mask))
        return;
    cache_t c = {.version = 1};
    memcpy(c.quotes, quotes, sizeof(quotes));
    if (nvs_set_blob(settings, "quote_cache", &c, sizeof(c)) == ESP_OK &&
        nvs_commit(settings) == ESP_OK) {
        cache_saved = now;
        cache_valid_mask = valid_mask;
    }
}
static void apply_config(const stock_config_t *candidate) {
    stock_config_t next = *candidate;
    for (unsigned i = 0; i < STOCK_MAX; i++)
        for (unsigned j = 0; j < STOCK_MAX; j++)
            if (next.alerts[i].kind && alerts[j].kind == next.alerts[i].kind &&
                next.alerts[i].target == alerts[j].target &&
                !strcmp(next.alerts[i].code, alerts[j].code))
                next.alerts[i].fired_date = alerts[j].fired_date;
    if (!persist(&next)) {
        strcpy(status, "设置保存失败，请重试");
        return;
    }
    stock_quote_t old[STOCK_MAX];
    memcpy(old, quotes, sizeof(old));
    watch = next.watch;
    prefs = next.prefs;
    memcpy(alerts, next.alerts, sizeof(alerts));
    align_quotes(old);
    selected = selected <= watch.count ? selected : watch.count;
    save_cache(true);
    strcpy(status, "手机设置已保存");
    request(page == DETAIL || (page == MENU && menu_parent == DETAIL));
}
static bool web_snapshot(stock_web_state_t *out) {
    if (xSemaphoreTake(state_lock, pdMS_TO_TICKS(300)) != pdTRUE)
        return false;
    out->config = configuration();
    memcpy(out->quotes, quotes, sizeof(quotes));
    snprintf(out->status, sizeof(out->status), "%s", status);
    out->stale = stale;
    xSemaphoreGive(state_lock);
    return true;
}
static bool web_update(const stock_config_t *c) { return xQueueSend(commands, c, 0) == pdTRUE; }
static void sound_task(void *arg) {
    (void)arg;
    uint8_t event;
    for (;;) {
        xQueueReceive(sounds, &event, portMAX_DELAY);
        esp_err_t e = stock_sound_play();
        if (e == ESP_OK)
            ESP_LOGI("stocks", "alert audio completed");
        else
            ESP_LOGW("stocks", "alert audio failed: %s", esp_err_to_name(e));
    }
}
static void button(bsp_btn_t key, bsp_btn_ev_t event, void *arg) {
    (void)arg;
    if (event == BSP_BTN_CLICK || event == BSP_BTN_LONG) {
        stock_key_t input = {key, event};
        xQueueSend(keys, &input, 0);
    }
}
static void request(bool detail) {
    job_t job = {.generation = ++generation, .detail = detail, .period = period, .watch = watch};
    memcpy(job.code, detail_code, 7);
    busy = true;
    strcpy(status, "正在更新行情…");
    xQueueOverwrite(jobs, &job);
    uint32_t idle = (uint32_t)((esp_timer_get_time() - last_input) / 1000000);
    next_refresh = esp_timer_get_time() +
                   (int64_t)stock_refresh_seconds(&prefs, idle, has_alerts()) * 1000000;
}
static void request_preview(int64_t now) {
    if (page != LIST || busy || !watch.count)
        return;
    stock_order(&watch, quotes, (stock_sort_t)prefs.sort, order);
    list_first = stock_list_first(selected, watch.count + 1, list_first);
    unsigned priority[STOCK_MAX], n = 0;
    for (unsigned i = list_first; i < watch.count && i < list_first + 3; i++)
        priority[n++] = i;
    if (list_first + 3 < watch.count)
        priority[n++] = list_first + 3;
    if (list_first)
        priority[n++] = list_first - 1;
    for (unsigned i = 0; i < watch.count; i++) {
        bool found = false;
        for (unsigned j = 0; j < n; j++)
            found |= priority[j] == i;
        if (!found)
            priority[n++] = i;
    }
    for (unsigned i = 0; i < n; i++) {
        unsigned slot = order[priority[i]];
        if (!quotes[slot].valid ||
            (preview_attempt[slot] && now - preview_attempt[slot] < 30000000LL))
            continue;
        job_t job = {.generation = generation, .preview = true, .quote = quotes[slot]};
        memcpy(job.code, quotes[slot].code, sizeof(job.code));
        preview_attempt[slot] = now;
        preview_state[slot] = 1;
        busy = true;
        xQueueOverwrite(jobs, &job);
        break;
    }
}
static void worker(void *arg) {
    (void)arg;
    job_t job;
    for (;;) {
        xQueueReceive(jobs, &job, portMAX_DELAY);
        result_t *r = &network_result;
        memset(r, 0, sizeof(*r));
        r->generation = job.generation;
        r->detail = job.detail;
        r->preview = job.preview;
        r->quote = job.quote;
        r->period = job.period;
        if (!(xEventGroupGetBits(wifi) & READY))
            r->error = "网络未连接，显示缓存";
        else if (time(NULL) < 1704067200 && esp_netif_sntp_sync_wait(pdMS_TO_TICKS(8000)) != ESP_OK)
            r->error = "网络校时失败";
        else {
            if (job.preview)
                r->ok = stock_fetch_minutes(&job.quote, r->minutes, &r->minute_count,
                                            r->minute_date, &r->error);
            else {
                r->quotes_ok = stock_fetch_quotes(&job.watch, r->quotes, &r->error);
                r->ok = r->quotes_ok;
            }
            if (job.detail) {
                int at = stock_watch_find(&job.watch, job.code);
                if (at >= 0 && r->quotes[at].valid &&
                    !strcmp(r->quotes[at].code, job.code)) {
                    r->quote = r->quotes[at];
                    r->ok = stock_fetch_chart_for_quote(&r->quote, job.period, r->bars, &r->count,
                                                        r->minutes, &r->minute_count,
                                                        r->minute_date, &r->error);
                } else {
                    r->ok = stock_fetch_detail(job.code, job.period, &r->quote, r->bars,
                                               &r->count, r->minutes, &r->minute_count,
                                               r->minute_date, &r->error);
                    if (at >= 0 && r->quote.valid && !strcmp(r->quote.code, job.code))
                        r->quotes[at] = r->quote;
                    if (!r->quotes_ok) {
                        r->quotes_ok = true;
                        for (unsigned i = 0; i < job.watch.count; i++)
                            r->quotes_ok &= r->quotes[i].valid;
                    }
                }
            }
        }
        ESP_LOGI("stocks", "fetch %s generation=%lu period=%u success=%d bars=%u minutes=%u",
                 job.preview ? "preview" : job.detail ? job.code : "watchlist", (unsigned long)job.generation, job.period,
                 r->ok, r->count, r->minute_count);
        ESP_LOGI("stocks", "memory free=%u minimum=%u largest=%u net_stack_free=%u",
                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_8BIT),
                 (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT),
                 (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT),
                 (unsigned)uxTaskGetStackHighWaterMark(NULL));
        xQueueSend(results, &r, portMAX_DELAY);
        xSemaphoreTake(result_consumed, portMAX_DELAY);
    }
}
static void server_start(void) {
    if (server)
        return;
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.max_uri_handlers = 4;
    cfg.stack_size = 8192;
    cfg.lru_purge_enable = true;
    cfg.recv_wait_timeout = 5;
    if (httpd_start(&server, &cfg) == ESP_OK) {
        httpd_uri_t handlers[] = {
            {.uri = "/", .method = HTTP_GET, .handler = stock_web_page},
            {.uri = "/setup", .method = HTTP_POST, .handler = save_setup},
            {.uri = "/api/state", .method = HTTP_GET, .handler = stock_web_state},
            {.uri = "/api/config", .method = HTTP_POST, .handler = stock_web_config}};
        for (unsigned i = 0; i < 4; i++)
            httpd_register_uri_handler(server, &handlers[i]);
    }
}
static void start_setup(void) {
    wifi_config_t ap = {0};
    snprintf((char *)ap.ap.ssid, sizeof(ap.ap.ssid), "%s", ap_name);
    ap.ap.authmode = WIFI_AUTH_OPEN;
    ap.ap.max_connection = 1;
    ap.ap.channel = 1;
    esp_wifi_set_mode(WIFI_MODE_APSTA);
    esp_wifi_set_config(WIFI_IF_AP, &ap);
    server_start();
    ++generation;
    busy = false;
    page = NETWORK;
    strcpy(status, server ? "开放热点 / 手机管理" : "手机服务未启动");
    draw();
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

static void connect_wifi(const stock_wifi_credentials_t *chosen, stock_wifi_source_t source) {
    wifi_config_t sta = {0};
    memcpy(sta.sta.ssid, chosen->ssid, strlen(chosen->ssid));
    memcpy(sta.sta.password, chosen->password, strlen(chosen->password));
    sta.sta.threshold.authmode = chosen->password[0] ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;
    active_wifi_source = source;
    wifi_failed_attempts = 0;
    ESP_LOGI("stocks", "Wi-Fi credentials source=%s", source == STOCK_WIFI_SAVED ? "saved" : "builtin");
    esp_wifi_disconnect();
    esp_err_t err = esp_wifi_set_config(WIFI_IF_STA, &sta);
    if (err == ESP_OK)
        err = esp_wifi_connect();
    if (err != ESP_OK) {
        ESP_LOGW("stocks", "Wi-Fi connect setup failed: %s", esp_err_to_name(err));
        xEventGroupSetBits(wifi, LINK_LOST);
    }
}
static void connect_saved(void) {
    stock_wifi_credentials_t saved = {0}, selected_wifi = {0};
    size_t n = sizeof(saved.ssid);
    esp_err_t ssid_result = nvs_get_str(credentials, "ssid", saved.ssid, &n);
    n = sizeof(saved.password);
    esp_err_t pass_result = nvs_get_str(credentials, "password", saved.password, &n);
    if (ssid_result != ESP_OK || pass_result != ESP_OK)
        memset(&saved, 0, sizeof(saved));
    stock_wifi_source_t source = stock_wifi_choose(&selected_wifi, &saved, stock_wifi_builtin());
    if (source == STOCK_WIFI_NONE) {
        start_setup();
        return;
    }
    connect_wifi(&selected_wifi, source);
}
static void select_detail(const char *code) {
    memcpy(detail_code, code, 7);
    int index = stock_watch_find(&watch, code);
    if (bsp_lvgl_lock(-1)) {
        detail_quote = index >= 0 ? quotes[index] : (stock_quote_t){0};
        detail_fresh = index >= 0 && (fresh_mask & (1u << index)) &&
                       (xEventGroupGetBits(wifi) & READY);
        bar_count = minute_count = cursor = 0;
        bsp_lvgl_unlock();
    }
    page = DETAIL;
    request(true);
}
static void menu_open(page_t parent) {
    menu_parent = parent;
    menu_selected = 0;
    page = MENU;
}
static void edit_alert(void) {
    if (stock_watch_find(&watch, detail_code) < 0) {
        strcpy(status, "请先加入自选，再设预警");
        return;
    }
    stock_alert_t *a = NULL;
    for (unsigned i = 0; i < STOCK_MAX; i++)
        if (!strcmp(alerts[i].code, detail_code)) {
            a = &alerts[i];
            break;
        }
    edit_alert_kind = a && a->kind ? a->kind : STOCK_PRICE_ABOVE;
    unsigned target = a && a->kind ? (unsigned)(a->target / (a->kind <= 2 ? 10 : 1))
                                   : (unsigned)(detail_quote.price / 10);
    char digits[7];
    snprintf(digits, sizeof(digits), "%06u", target <= 999999 ? target : 999999);
    stock_wheel_init(&wheel, digits);
    page = ALERT;
}
static void toggle_watch(void) {
    stock_config_t c = configuration();
    bool added = stock_watch_find(&watch, detail_code) < 0;
    if (added) {
        if (!stock_watch_add(&c.watch, detail_code)) {
            strcpy(status, stock_symbol(detail_code, NULL) ? "自选已满，最多12只" : "代码无效，请调整数字");
            return;
        }
    } else {
        stock_watch_remove(&c.watch, detail_code);
        for (unsigned i = 0; i < STOCK_MAX; i++)
            if (!strcmp(c.alerts[i].code, detail_code))
                memset(&c.alerts[i], 0, sizeof(c.alerts[i]));
    }
    if (!persist(&c)) {
        strcpy(status, "保存失败");
        return;
    }
    stock_quote_t old[STOCK_MAX];
    memcpy(old, quotes, sizeof(old));
    if (added && detail_quote.valid && !strcmp(detail_quote.code, detail_code)) {
        old[c.watch.count - 1] = detail_quote;
        if (detail_fresh)
            fresh_mask |= (uint16_t)(1u << (c.watch.count - 1));
    }
    watch = c.watch;
    memcpy(alerts, c.alerts, sizeof(alerts));
    align_quotes(old);
    save_cache(true);
    strcpy(status, added ? "已加入自选" : "已删除自选");
    if (added && adding_from_list) {
        page = LIST;
        selected = watch.count;
        list_force_snap = true;
        added_hint = true;
        adding_from_list = false;
    }
}
static void alert_save(void) {
    stock_config_t c = configuration();
    unsigned slot = STOCK_MAX;
    for (unsigned i = 0; i < STOCK_MAX; i++)
        if (!strcmp(c.alerts[i].code, detail_code)) {
            slot = i;
            break;
        }
    if (slot == STOCK_MAX)
        for (unsigned i = 0; i < STOCK_MAX; i++)
            if (!c.alerts[i].kind) {
                slot = i;
                break;
            }
    if (slot == STOCK_MAX) {
        strcpy(status, "预警列表已满");
        return;
    }
    stock_alert_t a = {.kind = edit_alert_kind};
    memcpy(a.code, detail_code, 7);
    a.target = atoi(wheel.code) * (a.kind <= 2 ? 10 : 1);
    if (!stock_alert_valid(&a)) {
        strcpy(status, "价格需大于零，百分比不超过100");
        return;
    }
    if (c.alerts[slot].kind == a.kind && c.alerts[slot].target == a.target)
        a.fired_date = c.alerts[slot].fired_date;
    c.alerts[slot] = a;
    if (!persist(&c)) {
        strcpy(status, "预警保存失败");
        return;
    }
    memcpy(alerts, c.alerts, sizeof(alerts));
    strcpy(status, a.kind ? "预警已保存 / 每交易日一次" : "预警已关闭");
    page = DETAIL;
    request(true);
}
static void menu_action(void) {
    if (menu_parent == DETAIL) {
        if (menu_selected == 0) {
            if (bsp_lvgl_lock(-1)) {
                period = (period + 1) % 4;
                bar_count = minute_count = cursor = 0;
                bsp_lvgl_unlock();
            }
            page = DETAIL;
            request(true);
        } else if (menu_selected == 1) {
            if (bsp_lvgl_lock(-1)) {
                indicators = !indicators;
                bsp_lvgl_unlock();
            }
            page = DETAIL;
        } else if (menu_selected == 2)
            edit_alert();
        else if (menu_selected == 3) {
            toggle_watch();
            if (page != LIST)
                page = DETAIL;
        } else if (menu_selected == 4)
            page = DETAIL;
        else {
            page = LIST;
            adding_from_list = false;
        }
    } else {
        stock_config_t c = configuration();
        if (menu_selected == 0)
            c.prefs.sort = (c.prefs.sort + 1) % 3;
        else if (menu_selected == 1)
            c.prefs.power_save = !c.prefs.power_save;
        else if (menu_selected == 2)
            c.prefs.sound = !c.prefs.sound;
        else if (menu_selected == 3) {
            c.prefs.refresh_interval = (c.prefs.refresh_interval + 1) % 4;
        } else if (menu_selected == 4) {
            start_setup();
            return;
        } else if (menu_selected == 5) {
            page = LIST;
            request(false);
            return;
        } else if (menu_selected == 6) {
            ++generation;
            busy = false;
            stock_wheel_init(&wheel, "000001");
            adding_from_list = false;
            page = WHEEL;
            return;
        } else {
            page = LIST;
            return;
        }
        if (persist(&c)) {
            prefs = c.prefs;
            next_refresh = esp_timer_get_time();
            strcpy(status, "设置已保存");
        } else
            strcpy(status, "设置保存失败");
    }
}
static void handle(stock_key_t key) {
    last_input = esp_timer_get_time();
    if (dimmed) {
        dimmed = false;
        bsp_display_backlight(100);
    }
    if (alert_until > 0) {
        alert_until = 0;
        alert_banner[0] = 0;
        draw();
        return;
    }
    if (key.event == BSP_BTN_LONG) {
        if (key.key == BSP_BTN_OK) {
            if (page == LIST || page == DETAIL)
                menu_open(page);
            else if (page == ALERT) {
                page = MENU;
                menu_parent = DETAIL;
            } else {
                page = LIST;
                selected = selected <= watch.count ? selected : watch.count;
                adding_from_list = false;
            }
        } else if (key.key == BSP_BTN_DOWN) {
            if (page == DETAIL) {
                if (bsp_lvgl_lock(-1)) {
                    period = (period + 1) % 4;
                    bar_count = minute_count = cursor = 0;
                    bsp_lvgl_unlock();
                }
                request(true);
            } else if (page == LIST) {
                stock_config_t c = configuration();
                c.prefs.sort = (c.prefs.sort + 1) % 3;
                if (persist(&c))
                    prefs = c.prefs;
            } else if (page == ALERT)
                edit_alert_kind = (edit_alert_kind + 1) % 5;
        } else if (key.key == BSP_BTN_UP) {
            if (page == WHEEL || page == ALERT) {
                if (wheel.digit)
                    wheel.digit--;
            } else if (page == LIST)
                request(false);
            else if (page == DETAIL)
                toggle_watch();
        }
    } else if (key.event == BSP_BTN_CLICK) {
        if (page == LIST) {
            added_hint = false;
            if (key.key == BSP_BTN_UP)
                selected = stock_list_move(selected, watch.count, -1);
            else if (key.key == BSP_BTN_DOWN)
                selected = stock_list_move(selected, watch.count, 1);
            else if (selected == watch.count) {
                if (watch.count == STOCK_MAX)
                    strcpy(status, "自选已满，先删除一只");
                else {
                    ++generation;
                    busy = false;
                    stock_wheel_init(&wheel, "000001");
                    adding_from_list = true;
                    page = WHEEL;
                    strcpy(status, "六位代码 · 自动识别交易所");
                }
            } else {
                adding_from_list = false;
                select_detail(watch.codes[order[selected]]);
            }
        } else if (page == WHEEL || page == ALERT) {
            if (key.key == BSP_BTN_UP)
                stock_wheel_turn(&wheel, 1);
            else if (key.key == BSP_BTN_DOWN)
                stock_wheel_turn(&wheel, -1);
            else if (stock_wheel_next(&wheel)) {
                if (page == ALERT)
                    alert_save();
                else if (stock_symbol(wheel.code, NULL)) {
                    if (adding_from_list && stock_watch_find(&watch, wheel.code) >= 0)
                        strcpy(status, "已在自选中，请换代码");
                    else
                        select_detail(wheel.code);
                } else
                    strcpy(status, "代码无效，请调整数字");
            }
        } else if (page == DETAIL) {
            if (key.key == BSP_BTN_OK)
                request(true);
            else if (bsp_lvgl_lock(-1)) {
                unsigned count = period == STOCK_INTRADAY ? minute_count : bar_count;
                if (count) {
                    if (key.key == BSP_BTN_UP)
                        cursor = cursor ? cursor - 1 : 0;
                    else if (cursor + 1 < count)
                        cursor++;
                }
                bsp_lvgl_unlock();
            }
        } else if (page == MENU) {
            unsigned count = menu_parent == DETAIL ? 6 : 8;
            if (key.key == BSP_BTN_UP)
                menu_selected = menu_selected ? menu_selected - 1 : count - 1;
            else if (key.key == BSP_BTN_DOWN)
                menu_selected = (menu_selected + 1) % count;
            else
                menu_action();
        }
    }
    draw();
}
static void process_result(result_t *r) {
    if (r->generation != generation)
        return;
    busy = false;
    if (r->preview) {
        int at = stock_watch_find(&watch, r->quote.code);
        if (at >= 0 && r->ok &&
            stock_preview_build(&previews[at], &r->quote, r->minutes, r->minute_count,
                                r->minute_date)) {
            preview_version[at]++;
            preview_state[at] = 3;
        } else if (at >= 0)
            preview_state[at] = 2;
        draw();
        return;
    }
    // Detail charts can fail independently of the bulk quote refresh.
    bool online = (xEventGroupGetBits(wifi) & READY) != 0;
    fresh_mask = online ? stock_quote_fresh_mask(&watch, r->quotes) : 0;
    uint16_t complete_mask = (uint16_t)((1u << watch.count) - 1u);
    stale = !online || !r->quotes_ok || fresh_mask != complete_mask;
    bool fired = false, latch_saved = true;
    for (unsigned i = 0; i < watch.count; i++)
        if (r->quotes[i].valid && !strcmp(r->quotes[i].code, watch.codes[i])) {
            quotes[i] = r->quotes[i];
            for (unsigned j = 0; j < STOCK_MAX; j++)
                if (stock_alert_evaluate(&alerts[j], &r->quotes[i], true)) {
                    snprintf(alert_banner, sizeof(alert_banner), "%s %s\n预警触发 · %.2f",
                             quotes[i].code, quotes[i].name, quotes[i].price / 1000.0);
                    alert_until = esp_timer_get_time() + 15000000LL;
                    fired = true;
                }
        }
    if (fired) {
        stock_config_t c = configuration();
        latch_saved = persist(&c);
        if (prefs.sound) {
            uint8_t sound = 1;
            xQueueSend(sounds, &sound, 0);
        }
        bsp_display_backlight(100);
        dimmed = false;
        last_input = esp_timer_get_time();
    }
    if (r->detail && bsp_lvgl_lock(-1)) {
        detail_fresh = online && r->quote.valid && !strcmp(r->quote.code, detail_code);
        if (r->quote.valid)
            detail_quote = r->quote;
        if (r->ok && r->period == period) {
            if (r->period == STOCK_INTRADAY)
                memcpy(minutes, r->minutes, r->minute_count * sizeof(*minutes));
            else
                memcpy(bars, r->bars, r->count * sizeof(*bars));
            bar_count = r->count;
            minute_count = r->minute_count;
            memcpy(minute_date, r->minute_date, sizeof(minute_date));
            cursor = (period == STOCK_INTRADAY ? minute_count : bar_count) - 1;
        }
        bsp_lvgl_unlock();
    }
    const char *stamp = r->detail ? detail_quote.stamp : (watch.count ? quotes[0].stamp : "");
    if (!r->ok)
        snprintf(status, sizeof(status), "%s%s", r->error ? r->error : "查询失败",
                 r->detail && r->quote.valid ? "" : " · 旧数据");
    else if (stamp[0])
        snprintf(status, sizeof(status), "行情 %.2s-%.2s %.2s:%.2s", stamp + 4, stamp + 6,
                 stamp + 8, stamp + 10);
    else
        strcpy(status, "暂无自选 · OK添加");
    if (!latch_saved)
        strcpy(status, "预警记录保存失败");
    save_cache(false);
    draw();
}
static void ui_task(void *arg) {
    (void)arg;
    int64_t next_battery = 0, retry = 0;
    bool was_ready = false;
    for (;;) {
        stock_key_t key;
        bool input = xQueueReceive(keys, &key, pdMS_TO_TICKS(40)) == pdTRUE;
        xSemaphoreTake(state_lock, portMAX_DELAY);
        if (input)
            handle(key);
        stock_config_t command;
        if (xQueueReceive(commands, &command, 0)) {
            apply_config(&command);
            draw();
        }
        if (atomic_exchange_explicit(&list_scroll_finished, false, memory_order_relaxed) &&
            page == LIST)
            draw();
        result_t *r;
        if (xQueueReceive(results, &r, 0)) {
            process_result(r);
            xSemaphoreGive(result_consumed);
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
            wifi_failed_attempts = 0;
            server_start();
            if (page == NETWORK)
                page = LIST;
            esp_wifi_set_mode(WIFI_MODE_STA);
            esp_wifi_set_ps(WIFI_PS_MIN_MODEM);
            request(page == DETAIL);
            draw();
        }
        if (!ready && was_ready) {
            stale = true;
            fresh_mask = 0;
            detail_fresh = false;
            strcpy(status, "网络断开 · 显示离线缓存");
            draw();
        }
        was_ready = ready;
        if (!ready && (bits & LINK_LOST) && now >= retry) {
            if (stock_wifi_try_builtin_after_retries(active_wifi_source, wifi_failed_attempts,
                                                      stock_wifi_builtin())) {
                stock_wifi_credentials_t fallback = {0};
                if (stock_wifi_choose(&fallback, NULL, stock_wifi_builtin()) == STOCK_WIFI_BUILTIN)
                    connect_wifi(&fallback, STOCK_WIFI_BUILTIN);
            } else {
                esp_wifi_connect();
                if (wifi_failed_attempts < 3)
                    wifi_failed_attempts++;
            }
            retry = now + 10000000LL;
        }
        if (ready && !busy && now >= next_refresh) {
            request(page == DETAIL || (page == MENU && menu_parent == DETAIL));
            draw();
        }
        if (ready)
            request_preview(now);
        bool should_dim = prefs.power_save && now - last_input >= 60000000LL;
        if (should_dim != dimmed) {
            dimmed = should_dim;
            bsp_display_backlight(dimmed ? 20 : 100);
        }
        if (alert_until > 0 && now >= alert_until) {
            alert_until = 0;
            alert_banner[0] = 0;
            draw();
        }
        if (now >= next_battery) {
            battery = bsp_battery_soc();
            next_battery = now + 15000000LL;
            draw();
        }
        xSemaphoreGive(state_lock);
    }
}
static void style_bg_color(lv_obj_t *object, lv_color_t value, lv_style_selector_t selector) {
    if (!lv_color_eq(lv_obj_get_style_bg_color(object, selector), value))
        lv_obj_set_style_bg_color(object, value, selector);
}
static void style_border_color(lv_obj_t *object, lv_color_t value, lv_style_selector_t selector) {
    if (!lv_color_eq(lv_obj_get_style_border_color(object, selector), value))
        lv_obj_set_style_border_color(object, value, selector);
}
static void style_text_color(lv_obj_t *object, lv_color_t value, lv_style_selector_t selector) {
    if (!lv_color_eq(lv_obj_get_style_text_color(object, selector), value))
        lv_obj_set_style_text_color(object, value, selector);
}
static void style_radius(lv_obj_t *object, int32_t value, lv_style_selector_t selector) {
    if (lv_obj_get_style_radius(object, selector) != value)
        lv_obj_set_style_radius(object, value, selector);
}
static void text_set(lv_obj_t *object, const char *value) {
    const char *current = lv_label_get_text(object);
    if (!current || strcmp(current, value))
        lv_label_set_text(object, value);
}
static void text_fmt(lv_obj_t *object, const char *format, ...) LV_FORMAT_ATTRIBUTE(2, 3);
static void text_fmt(lv_obj_t *object, const char *format, ...) {
    char text[256];
    va_list args, copy;
    va_start(args, format);
    va_copy(copy, args);
    int count = vsnprintf(text, sizeof(text), format, copy);
    va_end(copy);
    if (count >= 0 && (size_t)count < sizeof(text))
        text_set(object, text);
    else
        lv_label_set_text_vfmt(object, format, args);
    va_end(args);
}
static void visible(lv_obj_t *o, bool yes) {
    bool hidden = lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN);
    if (yes && hidden)
        lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN);
    else if (!yes && !hidden)
        lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
}
static lv_obj_t *label(int x, int y, int w, uint32_t color) {
    lv_obj_t *o = lv_label_create(screen);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_width(o, w);
    style_text_color(o, lv_color_hex(color), 0);
    lv_label_set_long_mode(o, LV_LABEL_LONG_DOT);
    return o;
}
static lv_obj_t *box(int x, int y, int w, int h) {
    lv_obj_t *o = lv_obj_create(screen);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_pad_all(o, 0, 0);
    style_radius(o, 12, 0);
    lv_obj_set_style_border_width(o, 1, 0);
    style_border_color(o, lv_color_hex(0x22344e), 0);
    style_bg_color(o, lv_color_hex(0x111d2c), 0);
    return o;
}
static int price_y(int32_t v, int32_t low, int32_t high, int top, int height) {
    return top + height - (int)(((int64_t)v - low) * height / ((int64_t)high - low));
}
static void line_draw(lv_layer_t *layer, int x1, int y1, int x2, int y2, uint32_t color,
                      bool dashed) {
    lv_draw_line_dsc_t d;
    lv_draw_line_dsc_init(&d);
    d.width = 1;
    d.color = lv_color_hex(color);
    d.p1 = (lv_point_precise_t){x1, y1};
    d.p2 = (lv_point_precise_t){x2, y2};
    if (dashed) {
        d.dash_width = 2;
        d.dash_gap = 3;
    }
    lv_draw_line(layer, &d);
}
static void rect_draw(lv_layer_t *layer, int x, int y, int w, int h, uint32_t color) {
    if (h < 1)
        h = 1;
    if (w < 1)
        w = 1;
    lv_area_t a = {x, y, x + w - 1, y + h - 1};
    lv_draw_rect_dsc_t d;
    lv_draw_rect_dsc_init(&d);
    d.bg_color = lv_color_hex(color);
    d.bg_opa = LV_OPA_COVER;
    lv_draw_rect(layer, &d, &a);
}
static unsigned minute_slot(unsigned hhmm) {
    unsigned minutes_of_day = (hhmm / 100) * 60 + hhmm % 100;
    return hhmm <= 1130 ? minutes_of_day - 570 : 121 + minutes_of_day - 780;
}
static bool minute_date_matches(void) {
    return detail_quote.valid && !strncmp(minute_date, detail_quote.stamp, 4) &&
           !strncmp(minute_date + 5, detail_quote.stamp + 4, 2) &&
           !strncmp(minute_date + 8, detail_quote.stamp + 6, 2);
}
static void chart_draw(lv_event_t *event) {
    if (period == STOCK_INTRADAY && !minute_date_matches())
        return;
    unsigned count = period == STOCK_INTRADAY ? minute_count : bar_count;
    if (!count)
        return;
    lv_layer_t *layer = lv_event_get_layer(event);
    lv_area_t a;
    lv_obj_get_coords(chart, &a);
    int left = a.x1 + 6, top = a.y1 + 7, width = lv_area_get_width(&a) - 12, height = 65,
        vol_top = top + 74;
    unsigned active = cursor < count ? cursor : count - 1;
    if (period == STOCK_INTRADAY) {
        int32_t low = detail_quote.previous, high = low;
        for (unsigned i = 0; i < count; i++) {
            if (minutes[i].price < low)
                low = minutes[i].price;
            if (minutes[i].price > high)
                high = minutes[i].price;
        }
        int32_t center = detail_quote.previous;
        int64_t spread = (int64_t)high - center;
        if ((int64_t)center - low > spread)
            spread = (int64_t)center - low;
        if (spread < 10)
            spread = 10;
        int64_t lower = (int64_t)center - spread, upper = (int64_t)center + spread;
        low = lower < 0 ? 0 : (int32_t)lower;
        high = upper > INT32_MAX ? INT32_MAX : (int32_t)upper;
        line_draw(layer, left, top + height / 2, left + width, top + height / 2, MUTED, true);
        line_draw(layer, left + width / 2, top, left + width / 2, vol_top + 18, 0x22344e, false);
        uint64_t maxvol = 1;
        unsigned step = (count + 79) / 80;
        for (unsigned i = 0; i < count; i += step) {
            unsigned end = i + step < count ? i + step : count;
            uint64_t v = minutes[end - 1].volume - (i ? minutes[i - 1].volume : 0);
            if (v > maxvol)
                maxvol = v;
        }
        for (unsigned i = 0; i < count; i += step) {
            unsigned next = i + step < count ? i + step : count - 1;
            int x = left + (int)(minute_slot(minutes[i].hhmm) * width / 241);
            int nx = left + (int)(minute_slot(minutes[next].hhmm) * width / 241);
            line_draw(layer, x, price_y(minutes[i].price, low, high, top, height), nx,
                      price_y(minutes[next].price, low, high, top, height), ACCENT, false);
            unsigned end = i + step < count ? i + step : count;
            uint64_t v = minutes[end - 1].volume - (i ? minutes[i - 1].volume : 0);
            int h = (int)(v * 18 / maxvol);
            rect_draw(layer, x, vol_top + 18 - h, 2, h,
                      minutes[i].price >= center ? UP_COLOR : DOWN_COLOR);
        }
        int x = left + (int)(minute_slot(minutes[active].hhmm) * width / 241);
        line_draw(layer, x, top, x, vol_top + 18, INK, true);
    } else {
        /* Forty visible candles keep draw allocations bounded; cursor scrolls all sixty. */
        unsigned start = active >= 39 ? active - 39 : 0,
                 end = start + 40 < count ? start + 40 : count, n = end - start;
        int32_t low = bars[start].low, high = bars[start].high;
        uint64_t maxvol = 1;
        for (unsigned i = start; i < end; i++) {
            if (bars[i].low < low)
                low = bars[i].low;
            if (bars[i].high > high)
                high = bars[i].high;
            if (bars[i].volume > maxvol)
                maxvol = bars[i].volume;
            if (indicators)
                for (unsigned p = 5; p <= 20; p *= 2) {
                    int32_t ma = stock_ma(bars, count, i, p);
                    if (ma && ma < low)
                        low = ma;
                    if (ma > high)
                        high = ma;
                }
        }
        if (high == low)
            high = low + 1;
        for (int j = 0; j < 3; j++)
            line_draw(layer, left, top + j * height / 2, left + width, top + j * height / 2,
                      0x22344e, false);
        for (unsigned i = start; i < end; i++) {
            stock_bar_t *b = &bars[i];
            int x = left + (int)(((i - start) * 2 + 1) * width / (n * 2));
            uint32_t color = b->close >= b->open ? UP_COLOR : DOWN_COLOR;
            line_draw(layer, x, price_y(b->high, low, high, top, height), x,
                      price_y(b->low, low, high, top, height), color, false);
            int y1 = price_y(b->open, low, high, top, height),
                y2 = price_y(b->close, low, high, top, height);
            if (y1 > y2) {
                int t = y1;
                y1 = y2;
                y2 = t;
            }
            int w = width / (int)n - 1;
            rect_draw(layer, x - w / 2, y1, w, y2 - y1 + 1, color);
            int h = (int)(b->volume * 18 / maxvol);
            rect_draw(layer, x - w / 2, vol_top + 18 - h, w, h, color);
        }
        if (indicators) {
            unsigned periods[] = {5, 10, 20};
            uint32_t colors[] = {GOLD, ACCENT, VIOLET};
            for (unsigned p = 0; p < 3; p++)
                for (unsigned i = start + 1; i < end; i++) {
                    int32_t a1 = stock_ma(bars, count, i - 1, periods[p]),
                            a2 = stock_ma(bars, count, i, periods[p]);
                    if (!a1 || !a2)
                        continue;
                    int x1 = left + (int)(((i - start - 1) * 2 + 1) * width / (n * 2)),
                        x2 = left + (int)(((i - start) * 2 + 1) * width / (n * 2));
                    line_draw(layer, x1, price_y(a1, low, high, top, height), x2,
                              price_y(a2, low, high, top, height), colors[p], false);
                }
        }
        int x = left + (int)(((active - start) * 2 + 1) * width / (n * 2));
        line_draw(layer, x, top, x, vol_top + 18, INK, true);
    }
}
static const char *sort_name(void) {
    return prefs.sort == STOCK_SORT_CODE     ? "代码排序"
           : prefs.sort == STOCK_SORT_CHANGE ? "涨幅排序"
                                             : "手动排序";
}
static const char *alert_name(unsigned kind) {
    static const char *names[] = {"关闭预警", "价格达到上限", "价格达到下限", "涨幅达到阈值",
                                  "跌幅达到阈值"};
    return kind < 5 ? names[kind] : names[0];
}
static void list_chrome_draw(lv_event_t *event) {
    if (!list_chrome_visible)
        return;
    lv_layer_t *layer = lv_event_get_layer(event);
    rect_draw(layer, 14, 55, 212, 1, 0x182638);
    rect_draw(layer, 220, 43, 4, 4, list_cached ? GOLD : DOWN_COLOR);
    if (list_display_count <= STOCK_LIST_VISIBLE)
        return;
    int height = 216, thumb = height * STOCK_LIST_VISIBLE / list_display_count;
    int maximum = (list_display_count - STOCK_LIST_VISIBLE) * STOCK_LIST_PITCH;
    int y = lv_obj_get_scroll_y(list_view);
    if (y < 0) y = 0;
    if (y > maximum) y = maximum;
    rect_draw(layer, 231, 66, 2, height, 0x162436);
    rect_draw(layer, 231, 66 + (height - thumb) * y / maximum, 2, thumb, 0x538ac0);
}
static void list_scroll_event(lv_event_t *event) {
    lv_event_code_t code = lv_event_get_code(event);
    if (code == LV_EVENT_SCROLL_BEGIN) {
        lv_anim_t *animation = lv_event_get_param(event);
        if (animation) {
            lv_anim_set_duration(animation, stock_scroll_duration(
                (unsigned)abs(animation->end_value - animation->start_value)));
            lv_anim_set_path_cb(animation, lv_anim_path_ease_out);
        }
    } else if (code == LV_EVENT_SCROLL_END)
        atomic_store_explicit(&list_scroll_finished, true, memory_order_relaxed);
    else if (code == LV_EVENT_SCROLL) {
        const lv_area_t track = {230, 60, 233, 287};
        lv_obj_invalidate_area(screen, &track);
    }
}
static void preview_draw(lv_event_t *event) {
    unsigned row = (unsigned)(uintptr_t)lv_event_get_user_data(event);
    if (!list_chrome_visible)
        return;
    lv_area_t area;
    lv_obj_get_coords(row_box[row], &area);
    lv_layer_t *layer = lv_event_get_layer(event);
    if (row_selected[row])
        rect_draw(layer, area.x1 + 1, area.y1 + 12, 2, 17, ACCENT);
    if (!row_preview_show[row])
        return;
    area.x1 += 6;
    area.y1 += 44;
    area.x2 = area.x1 + STOCK_SPARK_WIDTH - 1;
    area.y2 = area.y1 + STOCK_SPARK_HEIGHT - 1;
    line_draw(layer, area.x1, area.y1 + STOCK_SPARK_HEIGHT / 2, area.x2,
              area.y1 + STOCK_SPARK_HEIGHT / 2, 0x34465b, true);
    lv_draw_image_dsc_t d;
    lv_draw_image_dsc_init(&d);
    d.src = &preview_images[row];
    d.opa = row_preview_dim[row] ? LV_OPA_50 : LV_OPA_COVER;
    lv_draw_image(layer, &d, &area);
}
static void draw_list(void) {
    static unsigned bound[STOCK_LIST_SLOTS];
    static uint32_t rendered[STOCK_LIST_SLOTS];
    static bool valid_image[STOCK_LIST_SLOTS];
    static unsigned last_selected;
    stock_order(&watch, quotes, (stock_sort_t)prefs.sort, order);
    selected = selected <= watch.count ? selected : watch.count;
    list_first = stock_list_first(selected, watch.count + 1, list_first);
    if (list_display_count != watch.count + 1) {
        const lv_area_t track = {230, 60, 233, 287};
        lv_obj_invalidate_area(screen, &track);
    }
    if (list_cached != stale) {
        const lv_area_t dot = {219, 42, 225, 48};
        lv_obj_invalidate_area(screen, &dot);
    }
    list_display_count = watch.count + 1;
    list_cached = stale;
    text_fmt(subtitle, "%s · %u/12 · %s", sort_name(), watch.count,
             stale ? (fresh_mask ? "部分" : "缓存") : "在线");
    if (selected == watch.count)
        text_set(help, added_hint ? "已加入 · OK 继续添加" : "OK 输入代码 · 长OK 设置");
    else
        text_fmt(help, "%02u/%02u · OK 查看 · 长OK 设置", selected + 1, watch.count);
    lv_obj_set_pos(list_view, 14, 60);
    lv_obj_set_height(list_view, STOCK_LIST_VISIBLE * STOCK_LIST_PITCH);
    unsigned count = watch.count + 1;
    lv_obj_set_y(list_extent, count * STOCK_LIST_PITCH - 1);
    lv_obj_update_layout(list_view);
    int current = lv_obj_get_scroll_y(list_view);
    bool wrapped = count > 1 && ((last_selected == 0 && selected == count - 1) ||
                   (selected == 0 && last_selected == count - 1));
    stock_scroll_plan_t plan = stock_scroll_plan(current, list_first, count,
        drawn_page != LIST || wrapped || list_force_snap);
    list_force_snap = false;
    last_selected = selected;
    unsigned first = plan.first;
    for (unsigned row = 0; row < STOCK_LIST_SLOTS; row++) {
        unsigned i = first + row;
        bool show = i < count;
        visible(row_box[row], show);
        if (!show)
            continue;
        lv_obj_set_pos(row_box[row], 0, i * STOCK_LIST_PITCH);
        lv_obj_set_height(row_box[row], 72);
        row_selected[row] = i == selected;
        row_preview_show[row] = false;
        lv_obj_set_pos(row_name[row], 11, 4);
        lv_obj_set_pos(row_code[row], 11, 27);
        if (lv_label_get_long_mode(row_code[row]) != LV_LABEL_LONG_WRAP)
            lv_label_set_long_mode(row_code[row], LV_LABEL_LONG_WRAP);
        lv_obj_set_width(row_name[row], i < watch.count ? 104 : 194);
        lv_obj_set_width(row_code[row], i < watch.count ? 116 : 194);
        style_radius(row_box[row], 10, 0);
        style_bg_color(row_box[row], lv_color_hex(i == selected ? 0x152b40 :
                       i == watch.count ? 0x131c2b : 0x0e1826), 0);
        style_border_color(row_box[row], lv_color_hex(i == selected ? 0x355d7c : 0x1a2a3e), 0);
        visible(row_price[row], i < watch.count);
        visible(row_change[row], i < watch.count);
        style_text_color(row_name[row], lv_color_hex(i == watch.count ? GOLD : INK), 0);
        if (i == watch.count) {
            text_set(row_name[row], "+ 添加自选");
            text_set(row_code[row], watch.count == STOCK_MAX ? "已满 · 请先删除一只" : "OK 输入六位代码");
            bound[row] = 0;
            valid_image[row] = false;
            continue;
        }
        unsigned index = order[i];
        stock_quote_t *q = &quotes[index];
        bool fresh = (fresh_mask & (1u << index)) != 0;
        bool plotted = stock_preview_matches(&previews[index], q);
        row_preview_show[row] = plotted;
        bool dim = !fresh || preview_state[index] == 2;
        if (row_preview_dim[row] != dim && plotted)
            lv_obj_invalidate(row_box[row]);
        row_preview_dim[row] = dim;
        text_set(row_name[row], q->valid ? q->name : watch.codes[index]);
        char metadata[96];
        if (q->valid) {
            text_fmt(row_price[row], "%.2f", q->price / 1000.0);
            text_fmt(row_change[row], "%+.2f%%", q->change_bp / 100.0);
            snprintf(metadata, sizeof(metadata), "%s %s%.2s:%.2s", q->code,
                     fresh ? "" : "*", q->stamp + 8, q->stamp + 10);
        } else {
            snprintf(metadata, sizeof(metadata), "等待行情");
            text_set(row_price[row], "--");
            text_set(row_change[row], "--");
        }
        if (!plotted && q->valid) {
            size_t used = strlen(metadata);
            snprintf(metadata + used, sizeof(metadata) - used, "\n%s",
                     preview_state[index] == 1 ? "正在更新行情…"
                     : preview_state[index] == 2 ? "分时数据不可用" : "等待行情");
        }
        text_set(row_code[row], metadata);
        uint32_t color = !q->valid ? MUTED : q->change_bp >= 0 ? UP_COLOR : DOWN_COLOR;
        style_text_color(row_price[row], lv_color_hex(color), 0);
        style_text_color(row_change[row], lv_color_hex(color), 0);
        if (q->valid)
            style_bg_color(row_change[row], lv_color_hex(q->change_bp >= 0 ? 0x351f2c : 0x12332e), 0);
        lv_opa_t badge_opa = q->valid ? LV_OPA_COVER : LV_OPA_TRANSP;
        if (lv_obj_get_style_bg_opa(row_change[row], 0) != badge_opa)
            lv_obj_set_style_bg_opa(row_change[row], badge_opa, 0);
        bool palette_changed = row_curve_color[row] != color;
        row_curve_color[row] = color;
        if (bound[row] != index + 1 || rendered[row] != preview_version[index] ||
            valid_image[row] != plotted || palette_changed) {
            ((lv_color32_t *)preview_pixels[row])[1] = lv_color_to_32(lv_color_hex(color), LV_OPA_COVER);
            if (plotted)
                stock_preview_raster(&previews[index], preview_pixels[row] + 8);
            else
                memset(preview_pixels[row] + 8, 0, STOCK_SPARK_BYTES);
            lv_image_cache_drop(&preview_images[row]);
            lv_obj_invalidate(row_box[row]);
            bound[row] = index + 1;
            rendered[row] = preview_version[index];
            valid_image[row] = plotted;
        }
    }
    lv_obj_update_layout(list_view);
    lv_point_t end;
    lv_obj_get_scroll_end(list_view, &end);
    if (end.y != plan.target_y || (!plan.animate && current != plan.target_y))
        lv_obj_scroll_to_y(list_view, plan.target_y, plan.animate ? LV_ANIM_ON : LV_ANIM_OFF);
}
static void draw(void) {
    if (!bsp_lvgl_lock(-1))
        return;
    text_set(title, page == LIST     ? "自选行情"
                             : page == DETAIL ? "行情详情"
                             : page == WHEEL  ? "查询股票"
                             : page == MENU   ? (menu_parent == DETAIL ? "个股菜单" : "全局设置")
                             : page == ALERT  ? "设置预警"
                                              : "手机管理");
    if (battery >= 0)
        text_fmt(battery_label, "%d%%", battery);
    else
        text_set(battery_label, "--");
    if (page == LIST && drawn_page == LIST) {
        draw_list();
        visible(toast, alert_until > esp_timer_get_time());
        visible(toast_label, alert_until > esp_timer_get_time());
        text_set(toast_label, alert_banner);
        bsp_lvgl_unlock();
        return;
    }
    list_chrome_visible = page == LIST;
    text_set(status_label, status);
    style_text_color(status_label, lv_color_hex(stale ? GOLD : MUTED), 0);
    visible(status_label, page != DETAIL && page != LIST);
    visible(list_view, page == LIST || page == MENU);
    visible(subtitle, true);
    visible(hero, page == DETAIL);
    visible(name_label, page == DETAIL);
    visible(price_label, page == DETAIL);
    visible(change_label, page == DETAIL);
    visible(meta_label, page == DETAIL);
    visible(chart, page == DETAIL);
    visible(body, page == WHEEL || page == ALERT || page == NETWORK);
    for (unsigned i = 0; i < STOCK_LIST_SLOTS; i++) {
        bool rows = page == LIST || (page == MENU && i < 4);
        visible(row_box[i], rows);
        visible(row_name[i], rows);
        visible(row_code[i], rows);
        visible(row_price[i], rows && page == LIST);
        visible(row_change[i], rows && page == LIST);
        row_preview_show[i] = false;
        if (i < 4)
            visible(tabs[i], page == DETAIL);
    }
    for (unsigned i = 0; i < 6; i++) {
        visible(digit_box[i], page == WHEEL || page == ALERT);
        visible(digit_label[i], page == WHEEL || page == ALERT);
    }
    if (page == LIST) {
        draw_list();
    } else if (page == MENU) {
        lv_obj_scroll_to_y(list_view, 0, LV_ANIM_OFF);
        lv_obj_set_pos(list_view, 14, 68);
        lv_obj_set_height(list_view, 188);
        lv_obj_set_y(list_extent, 187);
        for (unsigned i = 0; i < 4; i++) {
            lv_obj_set_pos(row_box[i], 0, i * 47);
            lv_obj_set_height(row_box[i], 43);
            lv_obj_set_pos(row_name[i], 9, 4);
            lv_obj_set_pos(row_code[i], 9, 26);
            lv_label_set_long_mode(row_code[i], LV_LABEL_LONG_DOT);
        }
        unsigned at = menu_selected, count = menu_parent == DETAIL ? 6 : 8,
                 start = at >= 3 ? at - 3 : 0;
        text_set(subtitle, menu_parent == DETAIL ? "图表 / 预警 / 返回自选"
                                                 : "排序 / 刷新间隔 / 配网");
        for (unsigned row = 0; row < 4; row++) {
            unsigned i = start + row;
            bool show = i < count;
            visible(row_box[row], show);
            if (!show)
                continue;
            style_bg_color(row_box[row], lv_color_hex(i == at ? 0x193651 : 0x111d2c), 0);
            style_border_color(row_box[row], lv_color_hex(i == at ? 0x538ac0 : 0x22344e), 0);
            char text[64];
            const char *desc = "OK 执行";
            if (menu_parent == DETAIL) {
                const char *items[] = {"切换图表周期", "均线显示", "设置股票预警",
                                       "加入 / 删除自选", "返回详情", "返回自选列表"};
                snprintf(text, sizeof(text), "%s%s", items[i],
                         i == 1 ? (indicators ? " · 开" : " · 关") : "");
                if (i == 2)
                    desc = "价格 / 涨跌幅 · 每日一次";
            } else {
                const char *items[] = {"自选排序", "自动省电", "预警声音", "刷新间隔",
                                       "手机管理 / 配网", "立即刷新", "+ 添加自选 / 查询", "返回自选"};
                snprintf(text, sizeof(text), "%s%s", items[i],
                         i == 0   ? sort_name()
                         : i == 1 ? (prefs.power_save ? " · 开" : " · 关")
                         : i == 2 ? (prefs.sound ? " · 开" : " · 关")
                                  : "");
                if (i == 3) {
                    const unsigned seconds[] = {30, 60, 120, 300};
                    snprintf(text, sizeof(text), "%s · %u秒", items[i],
                             seconds[prefs.refresh_interval]);
                    desc = "空闲120秒 · 预警30秒";
                }
            }
            text_set(row_name[row], text);
            text_set(row_code[row], desc);
            lv_obj_set_width(row_name[row], 190);
            lv_obj_set_width(row_code[row], 190);
        }
        text_set(help, "OK 执行 · 长OK 返回自选");
    } else if (page == WHEEL || page == ALERT) {
        lv_obj_set_pos(body, 18, 176);
        text_set(subtitle,
                          page == ALERT ? alert_name(edit_alert_kind) : "UP/DOWN 转动 · OK 下一位");
        for (unsigned i = 0; i < 6; i++) {
            unsigned n = wheel.code[i] - '0';
            text_fmt(digit_label[i], "%u\n%u\n%u", (n + 1) % 10, n, (n + 9) % 10);
            style_bg_color(digit_box[i],
                                      lv_color_hex(i == wheel.digit ? 0x254d70 : 0x111d2c), 0);
            style_text_color(digit_label[i],
                                        lv_color_hex(i == wheel.digit ? INK : MUTED), 0);
        }
        if (page == ALERT) {
            int value = atoi(wheel.code);
            text_fmt(body,
                                  "%s · 第%u位\n阈值 %.2f %s\n长DOWN 切换预警类型\n最后一位OK 保存",
                                  detail_code, wheel.digit + 1, value / 100.0,
                                  edit_alert_kind <= STOCK_PRICE_BELOW ? "元" : "%");
        } else {
            char symbol[9];
            bool valid = stock_symbol(wheel.code, symbol);
            text_fmt(body, "第 %u / 6 位\n%s\n最后一位 OK 查询", wheel.digit + 1,
                                  valid ? (!strncmp(symbol, "sh", 2)   ? "上海 A 股"
                                           : !strncmp(symbol, "sz", 2) ? "深圳 A 股"
                                                                       : "北京 A 股")
                                        : "请输入有效代码");
        }
        text_set(help, "长UP 上一位 · 长OK 返回");
    } else if (page == DETAIL) {
        if (detail_quote.valid)
            text_fmt(subtitle, "%s · %.2s-%.2s %.2s:%.2s · %s", detail_code,
                                  detail_quote.stamp + 4, detail_quote.stamp + 6,
                                  detail_quote.stamp + 8, detail_quote.stamp + 10,
                                  !detail_fresh ? "缓存"
                                  : busy ? "更新"
                                         : "在线");
        else
            text_fmt(subtitle, "%s · %s", detail_code, busy ? "更新中" : "暂无数据");
        text_set(name_label, detail_quote.valid ? detail_quote.name : "等待行情");
        if (detail_quote.valid) {
            text_fmt(price_label, "%.2f", detail_quote.price / 1000.0);
            text_fmt(change_label, "%+.2f%%", detail_quote.change_bp / 100.0);
        } else {
            text_set(price_label, "--");
            text_set(change_label, "--");
        }
        uint32_t color = detail_quote.change_bp >= 0 ? UP_COLOR : DOWN_COLOR;
        style_text_color(price_label, lv_color_hex(color), 0);
        style_text_color(change_label, lv_color_hex(color), 0);
        const char *names[] = {"分时", "日K", "周K", "月K"};
        for (unsigned i = 0; i < 4; i++) {
            text_set(tabs[i], names[i]);
            style_text_color(tabs[i], lv_color_hex(i == (unsigned)period ? INK : MUTED),
                                        0);
            style_bg_color(tabs[i],
                                      lv_color_hex(i == (unsigned)period ? 0x254d70 : 0x111d2c), 0);
        }
        if (period == STOCK_INTRADAY && minute_count && minute_date_matches()) {
            stock_minute_t *m = &minutes[cursor < minute_count ? cursor : minute_count - 1];
            text_fmt(meta_label, "%s %02u:%02u  %.2f\n昨收 %.2f · 累计 %.0f手",
                                  minute_date, m->hhmm / 100, m->hhmm % 100, m->price / 1000.0,
                                  detail_quote.previous / 1000.0, (double)m->volume);
        } else if (period != STOCK_INTRADAY && bar_count) {
            stock_bar_t *b = &bars[cursor < bar_count ? cursor : bar_count - 1];
            text_fmt(
                meta_label, "%s  收 %.2f\n开 %.2f  高 %.2f  低 %.2f\n量 %.0f手 · %s", b->date,
                b->close / 1000.0, b->open / 1000.0, b->high / 1000.0, b->low / 1000.0,
                (double)b->volume, indicators ? "MA5/10/20" : "前复权");
        } else
            text_set(meta_label, busy ? "正在加载图表…" : status);
        lv_obj_invalidate(chart);
        text_set(help, adding_from_list ? "长UP 保存 · 长OK 菜单" :
                               "长DOWN 周期 · 长OK 菜单");
    } else {
        esp_netif_ip_info_t ip = {0};
        esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
        if (netif)
            esp_netif_get_ip_info(netif, &ip);
        text_set(subtitle, "批量自选 · 预警 · 设置");
        lv_obj_set_pos(body, 18, 70);
        text_fmt(
            body, "开放热点 %s\n浏览器 192.168.4.1\n\n同一Wi-Fi也可访问\n" IPSTR "\n\n无需热点密码",
            ap_name, IP2STR(&ip.ip));
        text_set(help, "长OK 返回自选");
    }
    visible(toast, alert_until > esp_timer_get_time());
    visible(toast_label, alert_until > esp_timer_get_time());
    text_set(toast_label, alert_banner);
    drawn_page = page;
    bsp_lvgl_unlock();
}
void app_main(void) {
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(nvs_open("stocks", NVS_READWRITE, &settings));
    ESP_ERROR_CHECK(nvs_open("worldcam", NVS_READWRITE, &credentials));
    stock_config_t saved = {0};
    size_t n = sizeof(saved);
    bool loaded = nvs_get_blob(settings, "board_v2", &saved, &n) == ESP_OK && n == sizeof(saved);
    bool migrated = loaded && saved.prefs.version == 1 && stock_config_migrate(&saved);
    if (loaded && stock_config_valid(&saved)) {
        watch = saved.watch;
        prefs = saved.prefs;
        memcpy(alerts, saved.alerts, sizeof(alerts));
        if (migrated && !persist(&saved))
            ESP_LOGW("stocks", "refresh settings migration not persisted");
    } else {
        stock_watch_t old = {0};
        n = sizeof(old);
        if (nvs_get_blob(settings, "watch_v1", &old, &n) == ESP_OK &&
            stock_watch_decode(&watch, &old, n)) {
        } else
            stock_watch_add(&watch, "000001");
        stock_config_t c = configuration();
        if (!persist(&c))
            ESP_LOGW("stocks", "settings migration not persisted");
    }
    cache_t cache = {0};
    n = sizeof(cache);
    if (nvs_get_blob(settings, "quote_cache", &cache, &n) == ESP_OK && n == sizeof(cache) &&
        cache.version == 1) {
        for (unsigned i = 0; i < STOCK_MAX; i++)
            if (cache.quotes[i].valid &&
                (strnlen(cache.quotes[i].code, 7) != 6 ||
                 strnlen(cache.quotes[i].name, STOCK_NAME_BYTES) == STOCK_NAME_BYTES ||
                 strnlen(cache.quotes[i].stamp, 15) != 14 ||
                 !stock_symbol(cache.quotes[i].code, NULL) || cache.quotes[i].price <= 0))
                cache.quotes[i].valid = false;
        align_quotes(cache.quotes);
        strcpy(status, "离线缓存 · 正在连接网络");
    }
    cache_saved = -300000000LL;
    last_input = esp_timer_get_time();
    ESP_ERROR_CHECK(bsp_display_init());
    if (!bsp_lvgl_init())
        abort();
    bsp_display_backlight(100);
    bsp_battery_init();
    keys = xQueueCreate(12, sizeof(stock_key_t));
    jobs = xQueueCreate(1, sizeof(job_t));
    results = xQueueCreate(1, sizeof(result_t *));
    commands = xQueueCreate(2, sizeof(stock_config_t));
    sounds = xQueueCreate(2, sizeof(uint8_t));
    wifi = xEventGroupCreate();
    state_lock = xSemaphoreCreateMutex();
    result_consumed = xSemaphoreCreateBinary();
    if (!keys || !jobs || !results || !commands || !sounds || !wifi || !state_lock ||
        !result_consumed)
        abort();
    stock_web_callbacks(web_snapshot, web_update);
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
    style_bg_color(screen, lv_color_hex(0x080d17), 0);
    lv_obj_set_style_text_font(screen, &stock_font_16, 0);
    lv_screen_load(screen);
    title = label(14, 15, 170, INK);
    battery_label = label(185, 17, 40, MUTED);
    lv_obj_set_style_text_font(battery_label, &lv_font_montserrat_12, 0);
    subtitle = label(14, 40, 200, MUTED);
    lv_obj_set_style_text_font(subtitle, &stock_font_12, 0);
    list_view = box(14, 60, 212, STOCK_LIST_VISIBLE * STOCK_LIST_PITCH);
    lv_obj_set_style_bg_opa(list_view, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(list_view, 0, 0);
    style_radius(list_view, 0, 0);
    lv_obj_add_event_cb(list_view, list_scroll_event, LV_EVENT_SCROLL_BEGIN, NULL);
    lv_obj_add_event_cb(list_view, list_scroll_event, LV_EVENT_SCROLL_END, NULL);
    lv_obj_add_event_cb(list_view, list_scroll_event, LV_EVENT_SCROLL, NULL);
    lv_obj_add_event_cb(screen, list_chrome_draw, LV_EVENT_DRAW_MAIN, NULL);
    lv_obj_set_scrollbar_mode(list_view, LV_SCROLLBAR_MODE_OFF);
    list_extent = lv_obj_create(list_view);
    lv_obj_remove_style_all(list_extent);
    lv_obj_set_size(list_extent, 1, 1);
    for (unsigned i = 0; i < STOCK_LIST_SLOTS; i++) {
        row_box[i] = box(0, i * STOCK_LIST_PITCH, 212, 72);
        lv_obj_set_parent(row_box[i], list_view);
        row_name[i] = label(9, 4, 113, INK);
        row_code[i] = label(9, 23, 112, MUTED);
        lv_obj_set_style_text_font(row_code[i], &stock_font_12, 0);
        row_price[i] = label(118, 0, 88, INK);
        lv_obj_set_style_text_align(row_price[i], LV_TEXT_ALIGN_RIGHT, 0);
        lv_obj_set_style_text_font(row_price[i], &lv_font_montserrat_20, 0);
        row_change[i] = label(138, 27, 65, MUTED);
        lv_obj_set_width(row_change[i], LV_SIZE_CONTENT);
        lv_obj_set_style_text_align(row_change[i], LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_font(row_change[i], &lv_font_montserrat_12, 0);
        lv_obj_set_parent(row_name[i], row_box[i]);
        lv_obj_set_parent(row_code[i], row_box[i]);
        lv_obj_set_parent(row_price[i], row_box[i]);
        lv_obj_set_parent(row_change[i], row_box[i]);
        lv_obj_set_style_pad_hor(row_change[i], 6, 0);
        lv_obj_set_style_pad_ver(row_change[i], 2, 0);
        style_radius(row_change[i], 4, 0);
        lv_obj_align(row_change[i], LV_ALIGN_TOP_RIGHT, -8, 25);
        preview_images[i] = (lv_image_dsc_t){
            .header = {.magic = LV_IMAGE_HEADER_MAGIC, .cf = LV_COLOR_FORMAT_I1,
                       .w = STOCK_SPARK_WIDTH, .h = STOCK_SPARK_HEIGHT,
                       .stride = STOCK_SPARK_STRIDE},
            .data_size = sizeof(preview_pixels[i]), .data = preview_pixels[i]};
        ((lv_color32_t *)preview_pixels[i])[1] = lv_color_to_32(lv_color_hex(ACCENT), LV_OPA_COVER);
        lv_obj_add_event_cb(row_box[i], preview_draw, LV_EVENT_DRAW_MAIN, (void *)(uintptr_t)i);
    }
    hero = box(14, 59, 212, 50);
    name_label = label(18, 59, 125, INK);
    price_label = label(18, 78, 143, INK);
    lv_obj_set_style_text_font(price_label, &lv_font_montserrat_32, 0);
    change_label = label(151, 86, 72, INK);
    lv_obj_set_style_text_font(change_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_align(change_label, LV_TEXT_ALIGN_RIGHT, 0);
    chart = box(14, 137, 212, 108);
    lv_obj_add_event_cb(chart, chart_draw, LV_EVENT_DRAW_MAIN, NULL);
    for (unsigned i = 0; i < 4; i++) {
        tabs[i] = label(14 + i * 54, 114, 50, MUTED);
        lv_obj_set_style_text_align(tabs[i], LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_bg_opa(tabs[i], LV_OPA_COVER, 0);
        style_radius(tabs[i], 6, 0);
        lv_obj_set_style_pad_ver(tabs[i], 2, 0);
        lv_obj_set_style_text_font(tabs[i], &stock_font_12, 0);
    }
    meta_label = label(14, 248, 212, MUTED);
    lv_label_set_long_mode(meta_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_line_space(meta_label, 0, 0);
    lv_obj_set_style_text_font(meta_label, &stock_font_12, 0);
    body = label(18, 176, 204, INK);
    lv_label_set_long_mode(body, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(body, &stock_font_12, 0);
    for (unsigned i = 0; i < 6; i++) {
        digit_box[i] = box(14 + i * 36, 70, 32, 88);
        digit_label[i] = label(22 + i * 36, 76, 20, INK);
        lv_label_set_long_mode(digit_label[i], LV_LABEL_LONG_WRAP);
        lv_obj_set_style_text_font(digit_label[i], &lv_font_montserrat_20, 0);
        lv_obj_set_style_text_line_space(digit_label[i], 6, 0);
    }
    status_label = label(14, 262, 212, MUTED);
    help = label(14, 299, 212, MUTED);
    lv_obj_set_style_text_font(help, &stock_font_12, 0);
    toast = box(14, 98, 212, 106);
    style_bg_color(toast, lv_color_hex(0x243a50), 0);
    style_border_color(toast, lv_color_hex(GOLD), 0);
    toast_label = label(25, 117, 190, GOLD);
    lv_label_set_long_mode(toast_label, LV_LABEL_LONG_WRAP);
    bsp_lvgl_unlock();
    ESP_ERROR_CHECK(bsp_button_init(button, NULL));
    connect_saved();
    draw();
    if (xTaskCreate(worker, "stocks_net", 8192, NULL, 4, NULL) != pdPASS ||
        xTaskCreate(ui_task, "stocks_ui", 8192, NULL, 5, NULL) != pdPASS ||
        xTaskCreate(sound_task, "stocks_sound", 4096, NULL, 3, NULL) != pdPASS)
        abort();
}
