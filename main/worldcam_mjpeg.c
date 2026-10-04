#include "worldcam_mjpeg.h"
#include <string.h>

static int refill(wc_mjpeg_reader_t *reader)
{
    if (!reader || !reader->source_read) return -1;
    int n = reader->source_read(reader->source_ctx, reader->raw, sizeof(reader->raw));
    if (n <= 0) {
        reader->raw_pos = 0;
        reader->raw_len = 0;
        return n;
    }
    reader->raw_pos = 0;
    reader->raw_len = (size_t)n;
    return n;
}

static int next_byte(wc_mjpeg_reader_t *reader, uint8_t *out)
{
    if (!reader || !out) return -1;
    if (reader->raw_pos >= reader->raw_len) {
        int n = refill(reader);
        if (n <= 0) return n;
    }
    *out = reader->raw[reader->raw_pos++];
    return 1;
}

void wc_mjpeg_reader_init(wc_mjpeg_reader_t *reader,
                          wc_mjpeg_source_read_fn source_read,
                          void *source_ctx,
                          size_t frame_limit)
{
    if (!reader) return;
    memset(reader, 0, sizeof(*reader));
    reader->source_read = source_read;
    reader->source_ctx = source_ctx;
    reader->frame_limit = frame_limit;
}

bool wc_mjpeg_begin_frame(wc_mjpeg_reader_t *reader)
{
    if (!reader || !reader->source_read) return false;
    if (reader->frame_open && !reader->frame_done && !reader->overflow)
        (void)wc_mjpeg_drain_frame(reader);

    reader->prefix_pos = 0;
    reader->prefix_len = 0;
    reader->frame_bytes = 0;
    reader->frame_open = false;
    reader->frame_done = false;
    reader->overflow = false;
    reader->previous_ff = false;

    bool saw_ff = false;
    for (size_t scanned = 0; scanned < WC_MJPEG_SCAN_LIMIT; ++scanned) {
        uint8_t b = 0;
        if (next_byte(reader, &b) <= 0) return false;
        if (saw_ff && b == 0xd8) {
            reader->prefix[0] = 0xff;
            reader->prefix[1] = 0xd8;
            reader->prefix_len = 2;
            reader->frame_open = true;
            reader->frame_bytes = 2;
            return true;
        }
        saw_ff = b == 0xff;
    }
    return false;
}

size_t wc_mjpeg_read_frame(uint8_t *dst, size_t max_bytes, void *ctx)
{
    wc_mjpeg_reader_t *reader = (wc_mjpeg_reader_t *)ctx;
    if (!reader || !dst || !max_bytes || !reader->frame_open ||
        reader->frame_done || reader->overflow)
        return 0;

    size_t written = 0;
    while (written < max_bytes && reader->prefix_pos < reader->prefix_len)
        dst[written++] = reader->prefix[reader->prefix_pos++];

    while (written < max_bytes && !reader->frame_done && !reader->overflow) {
        uint8_t b = 0;
        if (next_byte(reader, &b) <= 0) break;
        if (reader->frame_limit && reader->frame_bytes >= reader->frame_limit) {
            reader->overflow = true;
            break;
        }
        dst[written++] = b;
        ++reader->frame_bytes;

        if (reader->previous_ff && b == 0xd9) {
            reader->frame_done = true;
            reader->previous_ff = false;
            break;
        }
        reader->previous_ff = b == 0xff;
    }
    return written;
}

bool wc_mjpeg_drain_frame(wc_mjpeg_reader_t *reader)
{
    if (!reader || !reader->frame_open) return false;
    uint8_t discard[128];
    while (!reader->frame_done && !reader->overflow) {
        if (!wc_mjpeg_read_frame(discard, sizeof(discard), reader)) break;
    }
    return reader->frame_done && !reader->overflow;
}

bool wc_mjpeg_frame_overflow(const wc_mjpeg_reader_t *reader)
{
    return reader && reader->overflow;
}

size_t wc_mjpeg_frame_bytes(const wc_mjpeg_reader_t *reader)
{
    return reader ? reader->frame_bytes : 0;
}
