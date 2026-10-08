#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define HUB_APP_BYTES 64
#define HUB_TITLE_BYTES 96
#define HUB_BODY_BYTES 192
#define HUB_ATTR_STREAM_BYTES 512

typedef struct {
    uint32_t uid;
    uint8_t event;    /* 0 = added, 1 = modified, 2 = removed */
    uint8_t flags;
    uint8_t category;
    uint8_t count;
} hub_event_t;
typedef struct {
    uint32_t uid;
    char app[HUB_APP_BYTES];
    char title[HUB_TITLE_BYTES];
    char body[HUB_BODY_BYTES];
} hub_notice_t;
typedef struct {
    uint32_t expected_uid;
    size_t used;
    uint8_t bytes[HUB_ATTR_STREAM_BYTES];
} hub_decoder_t;

bool hub_event_parse(const uint8_t *bytes, size_t n, hub_event_t *out);
void hub_decoder_begin(hub_decoder_t *p, uint32_t uid);
/* returns true exactly when all app identifier, title, and message fields arrive.
 * Returns false for incomplete, malformed, oversized or unrelated responses. */
bool hub_decoder_feed(hub_decoder_t *p, const uint8_t *bytes,
                      size_t n, hub_notice_t *out);
/* Truncate at UTF-8 boundary; do not retain invalid tail bytes. */
void hub_utf8_copy(char *dst, size_t dst_size,
                   const uint8_t *src, size_t src_size);
