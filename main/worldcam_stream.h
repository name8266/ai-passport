#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef struct wc_stream wc_stream_t;

/* Open a long-lived HTTP(S) MJPEG/concatenated-JPEG stream. */
wc_stream_t *wc_stream_open(const char *url, int timeout_ms, const char **error);

/* Decode exactly one baseline JPEG frame into RGB565. */
bool wc_stream_next_frame(wc_stream_t *stream,
                          uint16_t width,
                          uint16_t height,
                          uint8_t *pixels,
                          const char **error);

void wc_stream_close(wc_stream_t *stream);
