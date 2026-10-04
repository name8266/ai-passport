#include "bsp_display.h"
#include "bsp_button.h"
#include "bsp_battery.h"
#include "worldcam_catalog.h"
#include "worldcam_protocol.h"
#include "worldcam_stream.h"
#include "jpeg_roi_decoder.h"
#include "lvgl.h"
#include "src/misc/cache/instance/lv_image_cache.h"
#include "esp_crt_bundle.h"
#include "esp_event.h"
#include "esp_http_client.h"
#include "esp_http_server.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include <time.h>
#include "esp_random.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/event_groups.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

LV_FONT_DECLARE(worldcam_font_16);
extern const lv_image_dsc_t worldcam_map;

#define WIFI_READY BIT0
#define WIFI_SETUP BIT1
#define WIFI_FAILED BIT2
#define NAME_BYTES 192
#define STREAM_URL_BYTES 256
#define MAX_SOURCE_BYTES (4u * 1024u * 1024u)
#define USAP_METADATA_BYTES 4096u
#define WC_HTTP_TIMEOUT_MS 6500
#define WC_MJPEG_TIMEOUT_MS 3500
#define WC_REFRESH_US 45000000LL
#define WC_RETRY_US 12000000LL
#define WC_RANDOM_RETRIES 3u
#define WC_CAMERA_FAILURE_LIMIT 2u
#define WC_MAX_REDIRECTS 4u
#define JPEG_SNIFF_BYTES 2048u

typedef enum { PAGE_MAP, PAGE_VIEW, PAGE_SETUP } page_t;
typedef struct { bsp_btn_t btn; bsp_btn_ev_t event; } input_t;
typedef struct { unsigned generation; uint16_t index; bool full; bool stream_test; } job_t;
typedef struct {
    job_t job;
    uint8_t *pixels;
    uint16_t width;
    uint16_t height;
    const char *error;
    bool source_failure;
    bool streaming;
    uint32_t frame_no;
} result_t;

typedef struct {
    esp_http_client_handle_t client;
    size_t total;
    size_t limit;
    bool over_limit;
    uint8_t prefix[JPEG_SNIFF_BYTES];
    size_t prefix_len;
    size_t prefix_pos;
} http_stream_t;

typedef struct {
    uint8_t *pixels;
    uint16_t width;
    uint16_t height;
} frame_sink_t;

static QueueHandle_t s_inputs, s_jobs, s_results;
static EventGroupHandle_t s_wifi;
static TaskHandle_t s_worker;
static nvs_handle_t s_nvs;
static char s_ap_name[32], s_ap_password[9];
static char s_test_stream_url[STREAM_URL_BYTES];
static httpd_handle_t s_httpd;
static page_t s_page;
static size_t s_selected;
static unsigned s_generation;
static int64_t s_next_refresh;
static bool s_busy, s_full, s_capitals_only;
static bool s_stream_test_pending, s_stream_mode;
static unsigned s_random_retries;
static uint8_t s_camera_failures[WC_CAMERA_COUNT];
static char s_name[NAME_BYTES], s_country[96];
static uint8_t *s_pixels;
static lv_image_dsc_t s_image_desc;
static lv_obj_t *s_title, *s_body, *s_country_label, *s_status, *s_help, *s_image, *s_battery, *s_map, *s_marker;
static lv_obj_t *s_header_rule, *s_map_card, *s_info_card, *s_view_frame, *s_status_pill, *s_live_dot;

static uint8_t s_jpeg_work[JPEG_DECODER_WORK_BUF_DEFAULT] __attribute__((aligned(4)));
static uint16_t s_jpeg_chunk[JPEG_CHUNK_BUF_PIXELS(WC_FULL_WIDTH)] __attribute__((aligned(4)));
static uint8_t s_jpeg_input[JPEG_INPUT_BUF_SIZE] __attribute__((aligned(4)));

static lv_obj_t *label(int x, int y, int width, uint32_t color)
{
    lv_obj_t *obj = lv_label_create(lv_screen_active());
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_width(obj, width);
    lv_obj_set_style_text_color(obj, lv_color_hex(color), 0);
    lv_label_set_text(obj, "");
    return obj;
}

static lv_obj_t *panel(int x, int y, int width, int height,
                       uint32_t background, uint32_t border, int radius)
{
    lv_obj_t *obj = lv_obj_create(lv_screen_active());
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_size(obj, width, height);
    lv_obj_set_style_bg_color(obj, lv_color_hex(background), 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(obj, lv_color_hex(border), 0);
    lv_obj_set_style_border_width(obj, 1, 0);
    lv_obj_set_style_radius(obj, radius, 0);
    lv_obj_set_style_pad_all(obj, 0, 0);
    return obj;
}

static void visible(lv_obj_t *obj, bool show)
{
    if (show) lv_obj_remove_flag(obj, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
}

static const wc_location_t *current_location(void)
{
    return &wc_locations[s_selected % WC_LOCATION_COUNT];
}

static unsigned camera_scenic_tier(uint16_t camera)
{
    if (camera >= WC_CAMERA_COUNT) return 3u;
    const char *url = wc_cameras[camera].snapshot;
    if (!url) return 3u;

    /* Tier 0 is deliberately conservative: these endpoints are either small
     * still-image feeds or official public webcam snapshots and have behaved
     * best with the ESP32-C3 baseline-JPEG decoder. */
    if (strstr(url, "foto-webcam.eu/") ||
        strstr(url, "nps.gov/webcams-") ||
        strstr(url, "geonet.org.nz/volcano/") ||
        strstr(url, "hko.gov.hk/") ||
        strstr(url, "usap.gov/"))
        return 0u;

    /* Skyline has excellent scenery coverage, but individual cameras can
     * occasionally change JPEG encoding or CDN behaviour. Keep it as the
     * second choice for random touring rather than the first. */
    if (strstr(url, "cdn.skylinewebcams.com/"))
        return 1u;

    return 2u;
}

static bool location_runtime_healthy(size_t index)
{
    if (index >= WC_LOCATION_COUNT) return false;
    const wc_location_t *loc = &wc_locations[index];
    if (!(loc->flags & WC_FLAG_AVAILABLE) ||
        loc->camera == WC_CAMERA_NONE ||
        loc->camera >= WC_CAMERA_COUNT) return false;
    return s_camera_failures[loc->camera] < WC_CAMERA_FAILURE_LIMIT;
}

static bool choose_scenic_location(size_t *chosen)
{
    if (!chosen) return false;
    size_t start = esp_random() % WC_LOCATION_COUNT;

    /* Prefer sources that are both scenic and friendly to the constrained
     * decoder. Only fall back to broader catalog providers when needed. */
    for (unsigned tier = 0; tier < 3; ++tier) {
        for (size_t offset = 0; offset < WC_LOCATION_COUNT; ++offset) {
            size_t index = (start + offset) % WC_LOCATION_COUNT;
            if (index == s_selected || !location_runtime_healthy(index)) continue;
            const wc_location_t *loc = &wc_locations[index];
            if (camera_scenic_tier(loc->camera) != tier) continue;
            *chosen = index;
            return true;
        }
        start = (start + WC_LOCATION_COUNT / 3 + 1) % WC_LOCATION_COUNT;
    }
    return false;
}

static void load_selected_labels(void)
{
    const wc_location_t *loc = current_location();
    snprintf(s_name, sizeof(s_name), "%s", loc->name);
    snprintf(s_country, sizeof(s_country), "%s", loc->country);
}

static const char *location_status(void)
{
    const wc_location_t *loc = current_location();
    if (!(loc->flags & WC_FLAG_HAS_SOURCE)) return "尚未找到公开可用画面";
    if (loc->camera < WC_CAMERA_COUNT &&
        s_camera_failures[loc->camera] >= WC_CAMERA_FAILURE_LIMIT)
        return "该实时源暂不可用，可按OK重试";
    if (loc->flags & WC_FLAG_AVAILABLE) return "LIVE实时快照 · 45秒刷新";
    return "来源暂不可用，可重试";
}

static void draw(const char *status)
{
    if (!bsp_lvgl_lock(-1)) return;
    bool full = s_page == PAGE_VIEW && s_full;
    visible(s_title, !full);
    visible(s_help, !full);
    visible(s_battery, !full);
    visible(s_header_rule, !full);
    visible(s_map_card, s_page == PAGE_MAP);
    visible(s_info_card, s_page == PAGE_MAP);
    visible(s_view_frame, s_page == PAGE_VIEW && !full);
    visible(s_status_pill, !full);
    visible(s_live_dot, s_page == PAGE_VIEW && !full);
    visible(s_map, s_page == PAGE_MAP);
    visible(s_marker, s_page == PAGE_MAP);
    visible(s_body, s_page != PAGE_VIEW);
    visible(s_country_label, s_page != PAGE_SETUP && !full);
    visible(s_image, s_page == PAGE_VIEW && s_pixels);
    visible(s_status, !full || !s_pixels);

    char text[480] = {0};
    if (s_page == PAGE_MAP) {
        lv_label_set_text(s_title, s_capitals_only ? "各国首都" : "世界之窗");
        lv_obj_set_pos(s_country_label, 24, 197);
        lv_label_set_text(s_country_label, s_country);
        lv_obj_set_pos(s_body, 24, 219);
        lv_obj_set_height(s_body, 24);
        lv_label_set_long_mode(s_body, LV_LABEL_LONG_DOT);
        int x, y;
        const wc_location_t *loc = current_location();
        wc_map_project(loc->lat, loc->lon, &x, &y);
        lv_obj_set_pos(s_marker, 12 + x - 5, 53 + y - 5);
        lv_label_set_text(s_body, s_name);
        lv_label_set_text(s_help, "前后选 · OK查看 · 长上精选\n长下筛首都 · 长OK设置");
    } else if (s_page == PAGE_VIEW) {
        lv_label_set_text(s_title, s_name[0] ? s_name : "地点画面");
        lv_obj_set_pos(s_country_label, 18, 52);
        lv_label_set_text(s_country_label, s_country);
        lv_obj_set_pos(s_image, s_full ? 0 : 24, s_full ? 80 : 84);
        lv_label_set_text(s_help, "OK刷新 · 长OK返回地图\n长下全屏 · 长上精选风景");
    } else {
        lv_label_set_text(s_title, "连接世界之窗");
        lv_obj_set_pos(s_body, 18, 64);
        lv_obj_set_height(s_body, 182);
        lv_label_set_long_mode(s_body, LV_LABEL_LONG_WRAP);
        snprintf(text, sizeof(text),
                 "手机连接热点：\n%s\n密码：%s\n\n浏览器打开：\n192.168.4.1\n配置2.4GHz Wi-Fi",
                 s_ap_name, s_ap_password);
        lv_label_set_text(s_body, text);
        lv_label_set_text(s_help, "需要2.4GHz无线网络\n长按OK返回地图");
    }
    lv_obj_set_pos(s_status, full ? 18 : 28, full ? 252 : 249);
    lv_obj_set_width(s_status, full ? 204 : 184);
    lv_label_set_text(s_status, status ? status : "");
    bsp_lvgl_unlock();
}

static void button(bsp_btn_t btn, bsp_btn_ev_t event, void *user)
{
    (void)user;
    if (event != BSP_BTN_CLICK && event != BSP_BTN_LONG) return;
    input_t input = {btn, event};
    (void)xQueueSend(s_inputs, &input, 0);
}

static void clear_pixels(void)
{
    if (!s_pixels || !bsp_lvgl_lock(-1)) return;
    lv_image_set_src(s_image, NULL);
    lv_image_cache_drop(&s_image_desc);
    free(s_pixels);
    s_pixels = NULL;
    bsp_lvgl_unlock();
}

static void queue_frame(void)
{
    const wc_location_t *loc = current_location();
    if (!(loc->flags & WC_FLAG_HAS_SOURCE)) {
        draw("尚未找到公开可用画面");
        return;
    }
    job_t job = {
        .generation = ++s_generation,
        .index = (uint16_t)s_selected,
        .full = s_full,
        .stream_test = false,
    };
    s_busy = true;
    s_next_refresh = esp_timer_get_time() + WC_REFRESH_US;
    xQueueOverwrite(s_jobs, &job);
    draw(s_pixels ? "正在切换实时画面… · 保留上一帧" : "正在获取实时画面…");
}

static void queue_stream_test(void)
{
    if (!s_test_stream_url[0]) {
        draw("未配置MJPEG测试地址");
        return;
    }
    job_t job = {
        .generation = ++s_generation,
        .index = (uint16_t)s_selected,
        .full = s_full,
        .stream_test = true,
    };
    s_stream_mode = true;
    s_busy = true;
    s_page = PAGE_VIEW;
    snprintf(s_name, sizeof(s_name), "%s", "MJPEG实时测试");
    snprintf(s_country, sizeof(s_country), "%s", "实验流");
    xQueueOverwrite(s_jobs, &job);
    draw(s_pixels ? "正在重连MJPEG流… · 保留上一帧" : "正在连接MJPEG实时流…");
}

static void select_location(size_t index, bool watch)
{
    size_t next = index % WC_LOCATION_COUNT;
    bool changed = next != s_selected;
    s_stream_mode = false;
    s_selected = next;
    load_selected_labels();
    s_page = watch ? PAGE_VIEW : PAGE_MAP;
    /* While switching cameras, keep the previous successful frame visible.
     * This avoids a blank screen during slow TLS/CDN responses. */
    if (changed && !watch) clear_pixels();
    if (watch) queue_frame();
    else draw(location_status());
}

static void random_tour(bool retry)
{
    size_t chosen;
    if (!choose_scenic_location(&chosen)) {
        memset(s_camera_failures, 0, sizeof(s_camera_failures));
        if (!choose_scenic_location(&chosen)) {
            draw("暂无可游览的实时画面");
            return;
        }
    }
    if (!retry) s_random_retries = WC_RANDOM_RETRIES;
    select_location(chosen, true);
}

static esp_http_client_handle_t open_https(const char *url, int64_t *content_length, const char **error)
{
    esp_http_client_config_t config = {
        .url = url,
        .timeout_ms = WC_HTTP_TIMEOUT_MS,
        .buffer_size = 2048,
        .buffer_size_tx = 512,
        .crt_bundle_attach = esp_crt_bundle_attach,
        /* We use the streaming open/fetch/read API, so handle redirects
         * explicitly instead of relying on esp_http_client_perform(). */
        .disable_auto_redirect = true,
        .user_agent = "PassportWorldCam/5.0",
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        *error = "内存不足";
        return NULL;
    }

    esp_http_client_set_header(client, "Accept", "image/jpeg,image/jpg;q=0.9,*/*;q=0.1");
    /* Do not let a CDN wrap JPEG bytes in gzip/br. The tiny decoder must see
     * the actual JPEG SOI marker at the start of the response body. */
    esp_http_client_set_header(client, "Accept-Encoding", "identity");
    esp_http_client_set_header(client, "Cache-Control", "no-cache");
    esp_http_client_set_header(client, "Pragma", "no-cache");
    esp_http_client_set_header(client, "Connection", "close");

    for (unsigned redirect = 0; redirect <= WC_MAX_REDIRECTS; ++redirect) {
        if (esp_http_client_open(client, 0) != ESP_OK) {
            *error = "无法连接公开摄像机";
            esp_http_client_cleanup(client);
            return NULL;
        }

        esp_http_client_set_timeout_ms(client, WC_HTTP_TIMEOUT_MS);
        int64_t length = esp_http_client_fetch_headers(client);
        if (length < 0) {
            *error = "源站响应超时，按OK重试";
            esp_http_client_close(client);
            esp_http_client_cleanup(client);
            return NULL;
        }

        int status = esp_http_client_get_status_code(client);
        if (status == 301 || status == 302 || status == 303 ||
            status == 307 || status == 308) {
            if (redirect == WC_MAX_REDIRECTS ||
                esp_http_client_set_redirection(client) != ESP_OK) {
                *error = "源站跳转过多";
                esp_http_client_close(client);
                esp_http_client_cleanup(client);
                return NULL;
            }
            esp_http_client_close(client);
            continue;
        }

        if (status != 200) {
            *error = "源站暂不可用，按OK重试";
            esp_http_client_close(client);
            esp_http_client_cleanup(client);
            return NULL;
        }
        if (length > (int64_t)MAX_SOURCE_BYTES) {
            *error = "实时画面文件过大";
            esp_http_client_close(client);
            esp_http_client_cleanup(client);
            return NULL;
        }
        if (content_length) *content_length = length;
        return client;
    }

    *error = "源站跳转过多";
    esp_http_client_cleanup(client);
    return NULL;
}

static bool fetch_usap_url(const char *metadata_url, char *resolved, size_t resolved_size, const char **error)
{
    int64_t length = 0;
    esp_http_client_handle_t client = open_https(metadata_url, &length, error);
    if (!client) return false;
    if (length > (int64_t)USAP_METADATA_BYTES) {
        *error = "地点资料过长";
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return false;
    }

    char body[USAP_METADATA_BYTES + 1];
    size_t total = 0;
    while (total < USAP_METADATA_BYTES) {
        int n = esp_http_client_read(client, body + total, USAP_METADATA_BYTES - total);
        if (n < 0) {
            *error = "地点资料下载失败";
            break;
        }
        if (n == 0) break;
        total += (size_t)n;
    }
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    if (*error) return false;
    body[total] = 0;
    if (!wc_resolve_usap(body, total, resolved, resolved_size)) {
        *error = "地点资料格式不正确";
        return false;
    }
    return true;
}

static size_t http_read_cb(uint8_t *dst, size_t max, void *vctx)
{
    http_stream_t *ctx = (http_stream_t *)vctx;
    if (!ctx || !ctx->client || ctx->total >= ctx->limit) {
        if (ctx) ctx->over_limit = true;
        return 0;
    }
    size_t remaining = ctx->limit - ctx->total;
    if (max > remaining) max = remaining;
    if (!max) {
        ctx->over_limit = true;
        return 0;
    }

    if (ctx->prefix_pos < ctx->prefix_len) {
        size_t available = ctx->prefix_len - ctx->prefix_pos;
        size_t n = max < available ? max : available;
        if (dst) memcpy(dst, ctx->prefix + ctx->prefix_pos, n);
        ctx->prefix_pos += n;
        ctx->total += n;
        return n;
    }

    if (dst) {
        int n = esp_http_client_read(ctx->client, (char *)dst, max);
        if (n <= 0) return 0;
        ctx->total += (size_t)n;
        return (size_t)n;
    }

    uint8_t discard[256];
    size_t want = max < sizeof(discard) ? max : sizeof(discard);
    int n = esp_http_client_read(ctx->client, (char *)discard, want);
    if (n <= 0) return 0;
    ctx->total += (size_t)n;
    return (size_t)n;
}

static int jpeg_scan_type(const uint8_t *data, size_t length)
{
    if (!data || length < 2 || data[0] != 0xff || data[1] != 0xd8) return -1;
    size_t i = 2;
    while (i + 3 < length) {
        if (data[i] != 0xff) {
            ++i;
            continue;
        }
        while (i < length && data[i] == 0xff) ++i;
        if (i >= length) break;
        uint8_t marker = data[i++];
        if (marker == 0xd9 || marker == 0xda) break;
        if (marker == 0x01 || (marker >= 0xd0 && marker <= 0xd7)) continue;
        if (i + 2 > length) break;
        size_t segment = ((size_t)data[i] << 8) | data[i + 1];
        if (segment < 2 || i + segment > length) break;
        if (marker == 0xc0) return 1;
        if (marker == 0xc2) return 2;
        i += segment;
    }
    return 0;
}

static bool jpeg_on_chunk(const jpeg_chunk_event_t *evt)
{
    frame_sink_t *sink = (frame_sink_t *)evt->user_data;
    if (!sink || !sink->pixels || evt->y >= sink->height || evt->x >= sink->width) return false;
    size_t max_bytes = (size_t)(sink->width - evt->x) * 2u;
    if (evt->byte_count > max_bytes) return false;
    memcpy(sink->pixels + (((size_t)evt->y * sink->width + evt->x) * 2u),
           evt->pixels, evt->byte_count);
    return true;
}

static void jpeg_on_done(const jpeg_done_event_t *evt)
{
    (void)evt;
}

static bool decode_camera_jpeg(esp_http_client_handle_t client, uint16_t width, uint16_t height,
                               uint8_t *pixels, const char **error)
{
    http_stream_t stream = {
        .client = client,
        .total = 0,
        .limit = MAX_SOURCE_BYTES,
        .over_limit = false,
        .prefix_len = 0,
        .prefix_pos = 0,
    };
    frame_sink_t sink = {
        .pixels = pixels,
        .width = width,
        .height = height,
    };
    memset(pixels, 0, (size_t)width * height * 2u);

    int prefetched = esp_http_client_read(
        client, (char *)stream.prefix, sizeof(stream.prefix));
    if (prefetched <= 0) {
        *error = "实时画面读取超时";
        return false;
    }
    stream.prefix_len = (size_t)prefetched;
    int jpeg_type = jpeg_scan_type(stream.prefix, stream.prefix_len);
    if (jpeg_type < 0) {
        *error = "源站返回的不是JPEG画面";
        return false;
    }
    if (jpeg_type == 2) {
        *error = "渐进JPEG不兼容，请换一个画面";
        return false;
    }

    jpeg_view_intent_t view = jpeg_view_default(width, height);
    view.reader = (jpeg_reader_t){ .cb = http_read_cb, .ctx = &stream };
    view.chunk_buffer = s_jpeg_chunk;
    view.input_buffer = s_jpeg_input;
    view.scale = JPEG_SCALE_AUTO;

    int decoded = jpeg_decoder_decode_view(
        &view,
        s_jpeg_work, sizeof(s_jpeg_work),
        jpeg_on_chunk, jpeg_on_done, &sink
    );
    if (decoded != JPEG_DECODE_OK) {
        *error = stream.over_limit ? "地点资料过长" : "画面格式不正确";
        return false;
    }
    return true;
}

static void worker(void *arg)
{
    (void)arg;
    job_t job;
    for (;;) {
        xQueueReceive(s_jobs, &job, portMAX_DELAY);
        result_t result = {.job = job};

        if (!(xEventGroupGetBits(s_wifi) & WIFI_READY)) {
            result.error = "无线网络未连接";
            goto done;
        }
        /* TLS certificate dates require a real clock after a cold boot.
         * Wait in the network worker so buttons and rendering stay responsive. */
        if (time(NULL) < 1704067200 &&
            esp_netif_sntp_sync_wait(pdMS_TO_TICKS(8000)) != ESP_OK) {
            result.error = "NTP未连接，按OK重试";
            goto done;
        }

        if (job.stream_test) {
            const char *error = NULL;
            wc_stream_t *stream = wc_stream_open(
                s_test_stream_url, WC_MJPEG_TIMEOUT_MS, &error);
            if (!stream) {
                result.streaming = true;
                result.error = error ? error : "MJPEG连接失败";
                goto done;
            }

            uint32_t frame_no = 0;
            while (job.generation == s_generation && s_stream_mode) {
                result_t frame = {
                    .job = job,
                    .streaming = true,
                    .frame_no = ++frame_no,
                    .width = job.full ? WC_FULL_WIDTH : WC_WIDTH,
                    .height = job.full ? WC_FULL_HEIGHT : WC_HEIGHT,
                };
                size_t bytes = (size_t)frame.width * frame.height * 2u;
                frame.pixels = malloc(bytes);
                if (!frame.pixels) {
                    frame.error = "MJPEG帧内存不足";
                    xQueueSend(s_results, &frame, portMAX_DELAY);
                    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
                    break;
                }

                if (!wc_stream_next_frame(
                        stream, frame.width, frame.height, frame.pixels, &error)) {
                    free(frame.pixels);
                    frame.pixels = NULL;
                    frame.error = error ? error : "MJPEG帧解码失败";
                    xQueueSend(s_results, &frame, portMAX_DELAY);
                    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
                    break;
                }

                xQueueSend(s_results, &frame, portMAX_DELAY);
                ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
            }
            wc_stream_close(stream);
            continue;
        }

        if (job.index >= WC_LOCATION_COUNT) {
            result.error = "地点坐标不正确";
            goto done;
        }

        const wc_location_t *loc = &wc_locations[job.index];
        if (loc->camera == WC_CAMERA_NONE || loc->camera >= WC_CAMERA_COUNT) {
            result.error = "尚未找到公开可用画面";
            goto done;
        }
        const wc_camera_t *camera = &wc_cameras[loc->camera];
        const char *source_url = camera->snapshot;
        char resolved[192];
        const char *error = NULL;
        if (camera->resolver == WC_RESOLVER_USAP) {
            if (!fetch_usap_url(camera->snapshot, resolved, sizeof(resolved), &error)) {
                result.error = error ? error : "南极站来源不可用";
                result.source_failure = true;
                goto done;
            }
            source_url = resolved;
        }

        int64_t content_length = 0;
        esp_http_client_handle_t client = open_https(source_url, &content_length, &error);
        if (!client) {
            result.error = error ? error : "源站连接失败";
            result.source_failure = true;
            goto done;
        }
        result.width = job.full ? WC_FULL_WIDTH : WC_WIDTH;
        result.height = job.full ? WC_FULL_HEIGHT : WC_HEIGHT;
        size_t bytes = (size_t)result.width * result.height * 2u;
        result.pixels = malloc(bytes);
        if (!result.pixels) {
            result.error = "内存不足";
            esp_http_client_close(client);
            esp_http_client_cleanup(client);
            goto done;
        }
        if (!decode_camera_jpeg(client, result.width, result.height, result.pixels, &error)) {
            free(result.pixels);
            result.pixels = NULL;
            result.error = error ? error : "画面格式不正确";
            result.source_failure = true;
        }
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
done:
        xQueueSend(s_results, &result, portMAX_DELAY);
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    }
}

static void wifi_event(void *arg, esp_event_base_t base, int32_t event, void *data)
{
    (void)arg;
    (void)data;
    if (base == WIFI_EVENT && event == WIFI_EVENT_STA_START) esp_wifi_connect();
    if (base == WIFI_EVENT && event == WIFI_EVENT_STA_DISCONNECTED) {
        xEventGroupClearBits(s_wifi, WIFI_READY);
        xEventGroupSetBits(s_wifi, WIFI_FAILED);
    }
    if (base == IP_EVENT && event == IP_EVENT_STA_GOT_IP) {
        ESP_ERROR_CHECK(esp_netif_sntp_start());
        xEventGroupClearBits(s_wifi, WIFI_FAILED);
        xEventGroupSetBits(s_wifi, WIFI_READY);
    }
}

static esp_err_t setup_page(httpd_req_t *req)
{
    static const char html[] =
        "<!doctype html><html lang='zh-CN'><meta name='viewport' content='width=device-width'>"
        "<title>世界之窗网络配置</title><style>body{font:16px system-ui;max-width:480px;margin:40px auto;"
        "padding:20px;background:#071820;color:#eef7f9}input,button{box-sizing:border-box;width:100%;"
        "padding:12px;margin:10px 0}button{background:#4de4bd;border:0}</style>"
        "<h1>世界之窗网络配置</h1><p>连接2.4GHz无线网络。</p>"
        "<form><label>2.4GHz Wi-Fi 名称<input name='ssid' maxlength='32' required></label>"
        "<label>密码<input name='password' type='password' maxlength='63'></label>"
        "<button>保存并连接</button></form><p id='status'></p><script>"
        "document.querySelector('form').onsubmit=async e=>{e.preventDefault();try{const r=await fetch('/setup',"
        "{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(Object.fromEntries(new FormData(e.target)))})"
        ";document.querySelector('#status').textContent=await r.text()}catch(e){document.querySelector('#status').textContent="
        "'请查看设备屏幕上的连接状态。'}}</script></html>";
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, html, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t save_setup(httpd_req_t *req)
{
    if (req->content_len <= 0 || req->content_len >= 256)
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "配置长度无效");
    char body[256];
    size_t read = 0;
    while (read < (size_t)req->content_len) {
        int n = httpd_req_recv(req, body + read, req->content_len - read);
        if (n <= 0) return ESP_FAIL;
        read += (size_t)n;
    }
    body[read] = 0;
    cJSON *root = cJSON_Parse(body);
    cJSON *ssid = cJSON_GetObjectItemCaseSensitive(root, "ssid");
    cJSON *password = cJSON_GetObjectItemCaseSensitive(root, "password");
    if (!cJSON_IsString(ssid) || !cJSON_IsString(password) ||
        !ssid->valuestring[0] || strlen(ssid->valuestring) > 32 ||
        strlen(password->valuestring) > 63 ||
        (strlen(password->valuestring) > 0 && strlen(password->valuestring) < 8)) {
        cJSON_Delete(root);
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "请检查 Wi-Fi 名称和密码");
    }

    esp_err_t err = nvs_set_str(s_nvs, "ssid", ssid->valuestring);
    if (err == ESP_OK) err = nvs_set_str(s_nvs, "password", password->valuestring);
    if (err == ESP_OK) (void)nvs_erase_key(s_nvs, "gateway");
    if (err == ESP_OK) err = nvs_commit(s_nvs);
    cJSON_Delete(root);
    if (err != ESP_OK) return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "无法保存配置");
    xEventGroupSetBits(s_wifi, WIFI_SETUP);
    return httpd_resp_sendstr(req, "已保存。请查看设备屏幕；连接失败时可在此更正。");
}

static void start_setup(void)
{
    wifi_config_t config = {0};
    snprintf((char *)config.ap.ssid, sizeof(config.ap.ssid), "%s", s_ap_name);
    snprintf((char *)config.ap.password, sizeof(config.ap.password), "%s", s_ap_password);
    config.ap.authmode = WIFI_AUTH_WPA2_PSK;
    config.ap.max_connection = 1;
    config.ap.channel = 1;
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &config));
    if (!s_httpd) {
        httpd_config_t http_config = HTTPD_DEFAULT_CONFIG();
        http_config.max_uri_handlers = 2;
        http_config.recv_wait_timeout = 5;
        if (httpd_start(&s_httpd, &http_config) == ESP_OK) {
            httpd_uri_t get = {.uri = "/", .method = HTTP_GET, .handler = setup_page};
            httpd_uri_t post = {.uri = "/setup", .method = HTTP_POST, .handler = save_setup};
            ESP_ERROR_CHECK(httpd_register_uri_handler(s_httpd, &get));
            ESP_ERROR_CHECK(httpd_register_uri_handler(s_httpd, &post));
        }
    }
    s_page = PAGE_SETUP;
    s_busy = false;
    ++s_generation;
    draw(s_httpd ? "网络配置" : "配置服务失败，请重启");
}

static void connect_saved(void)
{
    wifi_config_t config = {0};
    char ssid[33] = {0}, password[64] = {0};
    size_t size = sizeof(ssid);
    nvs_get_str(s_nvs, "ssid", ssid, &size);
    size = sizeof(password);
    nvs_get_str(s_nvs, "password", password, &size);
    if (!ssid[0]) {
        start_setup();
        return;
    }
    memcpy(config.sta.ssid, ssid, strlen(ssid));
    memcpy(config.sta.password, password, strlen(password));
    config.sta.threshold.authmode = password[0] ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;
    esp_wifi_disconnect();
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &config));
    esp_wifi_connect();
}

static size_t next_filtered(size_t from, int direction)
{
    size_t next = from;
    for (size_t i = 0; i < WC_LOCATION_COUNT; ++i) {
        next = wc_wrap_index(next, direction, WC_LOCATION_COUNT);
        if (!s_capitals_only || (wc_locations[next].flags & WC_FLAG_CAPITAL)) break;
    }
    return next;
}

static void handle_input(input_t input)
{
    if (input.event == BSP_BTN_LONG) {
        if (input.btn == BSP_BTN_OK) {
            ++s_generation;
            s_busy = false;
            s_random_retries = 0;
            if (s_page == PAGE_MAP) {
                start_setup();
                return;
            }
            s_page = PAGE_MAP;
            clear_pixels();
            draw(location_status());
        } else if (input.btn == BSP_BTN_UP && s_page != PAGE_SETUP) {
            random_tour(false);
        } else if (input.btn == BSP_BTN_DOWN && s_page != PAGE_SETUP) {
            if (s_page == PAGE_VIEW) {
                s_full = !s_full;
                queue_frame();
            } else {
                s_capitals_only = !s_capitals_only;
                if (s_capitals_only && !(current_location()->flags & WC_FLAG_CAPITAL))
                    s_selected = next_filtered(s_selected, 1);
                load_selected_labels();
                draw(location_status());
            }
        }
        return;
    }

    if (input.event != BSP_BTN_CLICK || s_page == PAGE_SETUP) return;
    if (input.btn == BSP_BTN_UP || input.btn == BSP_BTN_DOWN) {
        s_random_retries = 0;
        int direction = input.btn == BSP_BTN_UP ? -1 : 1;
        select_location(next_filtered(s_selected, direction), s_page == PAGE_VIEW);
    } else if (input.btn == BSP_BTN_OK) {
        s_random_retries = 0;
        if (!(current_location()->flags & WC_FLAG_HAS_SOURCE)) {
            draw("尚未找到公开可用画面");
            return;
        }
        if (s_page == PAGE_MAP) s_page = PAGE_VIEW;
        queue_frame();
    }
}

static void apply_result(result_t *result)
{
    bool retry_random = false;
    if (result->job.generation != s_generation) goto release;
    s_busy = false;
    const wc_location_t *loc =
        result->job.index < WC_LOCATION_COUNT ? &wc_locations[result->job.index] : NULL;
    if (result->error) {
        if (result->source_failure && loc && loc->camera < WC_CAMERA_COUNT &&
            s_camera_failures[loc->camera] < UINT8_MAX)
            ++s_camera_failures[loc->camera];
        draw(s_pixels ? "本次加载失败 · 保留上一帧" : result->error);
        s_next_refresh = esp_timer_get_time() + WC_RETRY_US;
        if (s_page == PAGE_VIEW && s_random_retries > 0) {
            --s_random_retries;
            retry_random = true;
        }
        goto release;
    }

    if (loc && loc->camera < WC_CAMERA_COUNT) s_camera_failures[loc->camera] = 0;
    if (!bsp_lvgl_lock(-1)) goto release;
    lv_image_set_src(s_image, NULL);
    lv_image_cache_drop(&s_image_desc);
    free(s_pixels);
    s_pixels = result->pixels;
    result->pixels = NULL;
    s_image_desc = (lv_image_dsc_t){
        .header = {
            .magic = LV_IMAGE_HEADER_MAGIC,
            .cf = LV_COLOR_FORMAT_RGB565,
            .w = result->width,
            .h = result->height,
            .stride = result->width * 2,
        },
        .data_size = (uint32_t)result->width * result->height * 2u,
        .data = s_pixels,
    };
    lv_image_set_src(s_image, &s_image_desc);
    bsp_lvgl_unlock();
    draw("LIVE · 45秒自动刷新");
    s_random_retries = 0;
    s_next_refresh = esp_timer_get_time() + WC_REFRESH_US;
release:
    free(result->pixels);
    xTaskNotifyGive(s_worker);
    if (retry_random) random_tour(true);
}

static void input_task(void *arg)
{
    (void)arg;
    int64_t next_battery = 0, next_connect = 0;
    bool was_ready = false;
    for (;;) {
        input_t input;
        if (xQueueReceive(s_inputs, &input, pdMS_TO_TICKS(30))) handle_input(input);
        result_t result;
        if (xQueueReceive(s_results, &result, 0)) apply_result(&result);

        EventBits_t bits = xEventGroupGetBits(s_wifi);
        if ((bits & WIFI_SETUP) && !s_busy) {
            xEventGroupClearBits(s_wifi, WIFI_SETUP | WIFI_FAILED | WIFI_READY);
            ++s_generation;
            connect_saved();
            draw("正在连接无线网络…");
        }

        bool ready = (xEventGroupGetBits(s_wifi) & WIFI_READY) != 0;
        if (ready && !was_ready) {
            if (s_httpd) {
                httpd_stop(s_httpd);
                s_httpd = NULL;
            }
            esp_wifi_set_mode(WIFI_MODE_STA);
            if (s_page == PAGE_SETUP) s_page = PAGE_MAP;
            draw("无线网络已连接");
        }
        was_ready = ready;

        int64_t now = esp_timer_get_time();
        if ((bits & WIFI_FAILED) && now >= next_connect) {
            next_connect = now + 10000000;
            esp_wifi_connect();
            if (s_page == PAGE_SETUP) draw("连接失败，请修改配置");
            else if (!s_busy) draw("无线网络已断开");
        }
        if (s_page == PAGE_VIEW && !s_busy &&
            (current_location()->flags & WC_FLAG_HAS_SOURCE) &&
            now >= s_next_refresh) {
            s_next_refresh = now + WC_REFRESH_US;
            queue_frame();
        }
        if (now >= next_battery) {
            next_battery = now + 30000000;
            int soc = bsp_battery_soc();
            if (bsp_lvgl_lock(-1)) {
                if (soc < 0) lv_label_set_text(s_battery, "--");
                else lv_label_set_text_fmt(s_battery, "%d%%", soc);
                bsp_lvgl_unlock();
            }
        }
    }
}

void app_main(void)
{
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(nvs_open("worldcam", NVS_READWRITE, &s_nvs));
    ESP_ERROR_CHECK(bsp_display_init());
    if (!bsp_lvgl_init()) return;
    bsp_battery_init();

    s_inputs = xQueueCreate(8, sizeof(input_t));
    s_jobs = xQueueCreate(1, sizeof(job_t));
    s_results = xQueueCreate(1, sizeof(result_t));
    s_wifi = xEventGroupCreate();
    if (!s_inputs || !s_jobs || !s_results || !s_wifi) abort();

    uint8_t mac[6];
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    if (!esp_netif_create_default_wifi_sta() || !esp_netif_create_default_wifi_ap()) abort();
    wifi_init_config_t wifi_config = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&wifi_config));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_get_mac(WIFI_IF_STA, mac));
    snprintf(s_ap_name, sizeof(s_ap_name), "WorldCam-%02X%02X", mac[4], mac[5]);
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event, NULL));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    esp_sntp_config_t clock_config = ESP_NETIF_SNTP_DEFAULT_CONFIG_MULTIPLE(
        3, ESP_SNTP_SERVER_LIST("ntp.aliyun.com", "ntp.tencent.com", "pool.ntp.org"));
    clock_config.start = false;
    ESP_ERROR_CHECK(esp_netif_sntp_init(&clock_config));
    ESP_ERROR_CHECK(esp_wifi_start());
    snprintf(s_ap_password, sizeof(s_ap_password), "%08lx", (unsigned long)esp_random());

    if (!bsp_lvgl_lock(-1)) return;
    lv_obj_t *screen = lv_obj_create(NULL);
    lv_obj_remove_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x071820), 0);
    lv_obj_set_style_text_font(screen, &worldcam_font_16, 0);
    lv_screen_load(screen);
    s_header_rule = panel(18, 47, 204, 1, 0x173A43, 0x173A43, 0);
    s_map_card = panel(8, 51, 224, 137, 0x0A2029, 0x173A43, 12);
    s_info_card = panel(14, 190, 212, 56, 0x0D2630, 0x214650, 10);
    s_view_frame = panel(19, 79, 202, 138, 0x0A2029, 0x214650, 10);
    s_status_pill = panel(20, 244, 200, 24, 0x0D2E32, 0x24605B, 12);
    s_live_dot = panel(205, 54, 7, 7, 0x4DE4BD, 0x4DE4BD, LV_RADIUS_CIRCLE);

    s_title = label(18, 20, 154, 0xF0F7F9);
    lv_label_set_long_mode(s_title, LV_LABEL_LONG_DOT);
    s_battery = label(178, 20, 44, 0xBDCED5);
    s_body = label(24, 219, 192, 0xF0F7F9);
    s_country_label = label(24, 197, 188, 0x7FCFC2);
    lv_label_set_long_mode(s_country_label, LV_LABEL_LONG_DOT);
    s_status = label(28, 249, 184, 0x4DE4BD);
    lv_label_set_long_mode(s_status, LV_LABEL_LONG_DOT);
    s_help = label(18, 276, 210, 0x8FA8AE);
    s_map = lv_image_create(screen);
    lv_image_set_src(s_map, &worldcam_map);
    lv_obj_set_pos(s_map, 12, 53);
    s_marker = lv_obj_create(screen);
    lv_obj_remove_flag(s_marker, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(s_marker, 11, 11);
    lv_obj_set_style_radius(s_marker, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(s_marker, lv_color_hex(0xFFCB66), 0);
    lv_obj_set_style_border_color(s_marker, lv_color_hex(0xFFF4D6), 0);
    lv_obj_set_style_border_width(s_marker, 2, 0);
    lv_obj_set_style_shadow_color(s_marker, lv_color_hex(0xFFCB66), 0);
    lv_obj_set_style_shadow_width(s_marker, 6, 0);
    s_image = lv_image_create(screen);
    lv_obj_set_pos(s_image, 24, 82);
    visible(s_image, false);
    bsp_lvgl_unlock();

    s_selected = 0;
    load_selected_labels();
    if (xTaskCreate(worker, "cam_net", 16384, NULL, 4, &s_worker) != pdPASS) abort();

    s_page = PAGE_MAP;
    draw("正在连接无线网络…");
    connect_saved();

    ESP_ERROR_CHECK(bsp_button_init(button, NULL));
    if (xTaskCreate(input_task, "cam_ui", 6144, NULL, 5, NULL) != pdPASS) abort();
}
