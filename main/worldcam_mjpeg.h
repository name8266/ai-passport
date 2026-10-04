#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define WC_MJPEG_RAW_BUFFER 768u
#define WC_MJPEG_SCAN_LIMIT (64u * 1024u)

typedef int (*wc_mjpeg_source_read_fn)(void *ctx, uint8_t *dst, size_t max_bytes);

typedef struct {
    wc_mjpeg_source_read_fn source_read;
    void *source_ctx;
    uint8_t raw[WC_MJPEG_RAW_BUFFER];
    size_t raw_pos;
    size_t raw_len;
    uint8_t prefix[2];
    size_t prefix_pos;
    size_t prefix_len;
    size_t frame_bytes;
    size_t frame_limit;
    bool frame_open;
    bool frame_done;
    bool overflow;
    bool previous_ff;
} wc_mjpeg_reader_t;

void wc_mjpeg_reader_init(wc_mjpeg_reader_t *reader,
                          wc_mjpeg_source_read_fn source_read,
                          void *source_ctx,
                          size_t frame_limit);

/* Finds the next JPEG SOI marker inside an MJPEG/multipart byte stream. */
bool wc_mjpeg_begin_frame(wc_mjpeg_reader_t *reader);

/* jpeg_reader_t-compatible callback: returns bytes from one JPEG only. */
size_t wc_mjpeg_read_frame(uint8_t *dst, size_t max_bytes, void *ctx);

/* Consumes any unread tail of the current JPEG through its EOI marker. */
bool wc_mjpeg_drain_frame(wc_mjpeg_reader_t *reader);

bool wc_mjpeg_frame_overflow(const wc_mjpeg_reader_t *reader);
size_t wc_mjpeg_frame_bytes(const wc_mjpeg_reader_t *reader);
