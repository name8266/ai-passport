#include "worldcam_stream.h"
#include "worldcam_mjpeg.h"
#include "jpeg_roi_decoder.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include <stdlib.h>
#include <string.h>

#define WC_STREAM_MAX_REDIRECTS 4u
#define WC_STREAM_FRAME_LIMIT (512u * 1024u)
#define WC_STREAM_SNIFF_BYTES 2048u
#define WC_STREAM_MAX_WIDTH 240u

typedef struct {
    uint8_t *pixels;
    uint16_t width;
    uint16_t height;
} frame_sink_t;

typedef struct {
    wc_mjpeg_reader_t *reader;
    uint8_t prefix[WC_STREAM_SNIFF_BYTES];
    size_t prefix_len;
    size_t prefix_pos;
} frame_input_t;

struct wc_stream {
    esp_http_client_handle_t client;
    wc_mjpeg_reader_t reader;
};

static uint8_t s_stream_jpeg_work[JPEG_DECODER_WORK_BUF_DEFAULT] __attribute__((aligned(4)));
static uint16_t s_stream_jpeg_chunk[JPEG_CHUNK_BUF_PIXELS(WC_STREAM_MAX_WIDTH)] __attribute__((aligned(4)));
static uint8_t s_stream_jpeg_input[JPEG_INPUT_BUF_SIZE] __attribute__((aligned(4)));

static int source_read(void *ctx, uint8_t *dst, size_t max_bytes)
{
    esp_http_client_handle_t client = (esp_http_client_handle_t)ctx;
    if (!client || !max_bytes) return -1;
    int n = esp_http_client_read(client, (char *)dst, (int)max_bytes);
    return n;
}

static size_t jpeg_read(uint8_t *dst, size_t max_bytes, void *ctx)
{
    frame_input_t *input = (frame_input_t *)ctx;
    if (!input || !max_bytes) return 0;

    size_t written = 0;
    while (written < max_bytes && input->prefix_pos < input->prefix_len) {
        uint8_t b = input->prefix[input->prefix_pos++];
        if (dst) dst[written] = b;
        ++written;
    }
    if (written < max_bytes) {
        size_t n = wc_mjpeg_read_frame(
            dst ? dst + written : NULL,
            max_bytes - written,
            input->reader
        );
        written += n;
    }
    return written;
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

wc_stream_t *wc_stream_open(const char *url, int timeout_ms, const char **error)
{
    if (error) *error = NULL;
    if (!url || !url[0]) {
        if (error) *error = "MJPEG地址为空";
        return NULL;
    }

    esp_http_client_config_t config = {
        .url = url,
        .timeout_ms = timeout_ms,
        .buffer_size = 2048,
        .buffer_size_tx = 512,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .disable_auto_redirect = true,
        .user_agent = "PassportWorldCam-MJPEG/0.1",
        .keep_alive_enable = true,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        if (error) *error = "MJPEG连接内存不足";
        return NULL;
    }

    esp_http_client_set_header(client, "Accept",
                               "multipart/x-mixed-replace,image/jpeg,*/*;q=0.1");
    esp_http_client_set_header(client, "Accept-Encoding", "identity");
    esp_http_client_set_header(client, "Cache-Control", "no-cache");
    esp_http_client_set_header(client, "Pragma", "no-cache");
    esp_http_client_set_header(client, "Connection", "keep-alive");

    for (unsigned redirect = 0; redirect <= WC_STREAM_MAX_REDIRECTS; ++redirect) {
        if (esp_http_client_open(client, 0) != ESP_OK) {
            if (error) *error = "无法连接MJPEG源";
            esp_http_client_cleanup(client);
            return NULL;
        }

        int64_t length = esp_http_client_fetch_headers(client);
        if (length < 0) {
            if (error) *error = "MJPEG源响应超时";
            esp_http_client_close(client);
            esp_http_client_cleanup(client);
            return NULL;
        }

        int status = esp_http_client_get_status_code(client);
        if (status == 301 || status == 302 || status == 303 ||
            status == 307 || status == 308) {
            if (redirect == WC_STREAM_MAX_REDIRECTS ||
                esp_http_client_set_redirection(client) != ESP_OK) {
                if (error) *error = "MJPEG源跳转过多";
                esp_http_client_close(client);
                esp_http_client_cleanup(client);
                return NULL;
            }
            esp_http_client_close(client);
            continue;
        }

        if (status != 200) {
            if (error) *error = "MJPEG源返回非200状态";
            esp_http_client_close(client);
            esp_http_client_cleanup(client);
            return NULL;
        }

        wc_stream_t *stream = calloc(1, sizeof(*stream));
        if (!stream) {
            if (error) *error = "MJPEG状态内存不足";
            esp_http_client_close(client);
            esp_http_client_cleanup(client);
            return NULL;
        }
        stream->client = client;
        wc_mjpeg_reader_init(&stream->reader, source_read, client, WC_STREAM_FRAME_LIMIT);
        return stream;
    }

    if (error) *error = "MJPEG源跳转过多";
    esp_http_client_cleanup(client);
    return NULL;
}

bool wc_stream_next_frame(wc_stream_t *stream,
                          uint16_t width,
                          uint16_t height,
                          uint8_t *pixels,
                          const char **error)
{
    if (error) *error = NULL;
    if (!stream || !pixels || !width || !height || width > WC_STREAM_MAX_WIDTH) {
        if (error) *error = "MJPEG帧参数无效";
        return false;
    }
    if (!wc_mjpeg_begin_frame(&stream->reader)) {
        if (error) *error = "MJPEG未收到JPEG帧";
        return false;
    }

    frame_input_t input = {
        .reader = &stream->reader,
        .prefix_len = 0,
        .prefix_pos = 0,
    };
    size_t prefetched = wc_mjpeg_read_frame(
        input.prefix, sizeof(input.prefix), &stream->reader);
    if (prefetched < 2) {
        if (error) *error = "MJPEG帧读取超时";
        return false;
    }
    input.prefix_len = prefetched;

    int jpeg_type = jpeg_scan_type(input.prefix, input.prefix_len);
    if (jpeg_type < 0) {
        if (error) *error = "MJPEG帧不是JPEG";
        return false;
    }
    if (jpeg_type == 2) {
        (void)wc_mjpeg_drain_frame(&stream->reader);
        if (error) *error = "MJPEG渐进JPEG不兼容";
        return false;
    }

    memset(pixels, 0, (size_t)width * height * 2u);
    frame_sink_t sink = {
        .pixels = pixels,
        .width = width,
        .height = height,
    };
    jpeg_view_intent_t view = jpeg_view_default(width, height);
    view.reader = (jpeg_reader_t){ .cb = jpeg_read, .ctx = &input };
    view.chunk_buffer = s_stream_jpeg_chunk;
    view.input_buffer = s_stream_jpeg_input;
    view.scale = JPEG_SCALE_AUTO;

    int decoded = jpeg_decoder_decode_view(
        &view,
        s_stream_jpeg_work, sizeof(s_stream_jpeg_work),
        jpeg_on_chunk, jpeg_on_done, &sink
    );
    bool complete = wc_mjpeg_drain_frame(&stream->reader);
    if (decoded != JPEG_DECODE_OK || !complete) {
        if (error) {
            *error = wc_mjpeg_frame_overflow(&stream->reader)
                ? "MJPEG单帧过大"
                : "MJPEG帧解码失败";
        }
        return false;
    }
    return true;
}

void wc_stream_close(wc_stream_t *stream)
{
    if (!stream) return;
    if (stream->client) {
        esp_http_client_close(stream->client);
        esp_http_client_cleanup(stream->client);
    }
    free(stream);
}
