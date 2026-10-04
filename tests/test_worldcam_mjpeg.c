#include "worldcam_mjpeg.h"
#include <assert.h>
#include <stdint.h>
#include <string.h>

typedef struct {
    const uint8_t *data;
    size_t length;
    size_t offset;
} fake_source_t;

static int fake_read(void *ctx, uint8_t *dst, size_t max_bytes)
{
    fake_source_t *src = (fake_source_t *)ctx;
    if (src->offset >= src->length) return 0;
    size_t remaining = src->length - src->offset;
    size_t n = remaining < max_bytes ? remaining : max_bytes;
    if (n > 3) n = 3; /* force boundaries to cross source reads */
    memcpy(dst, src->data + src->offset, n);
    src->offset += n;
    return (int)n;
}

static size_t read_one(wc_mjpeg_reader_t *reader, uint8_t *out, size_t cap)
{
    assert(wc_mjpeg_begin_frame(reader));
    size_t total = 0;
    while (total < cap) {
        size_t n = wc_mjpeg_read_frame(out + total, cap - total, reader);
        if (!n) break;
        total += n;
    }
    assert(wc_mjpeg_drain_frame(reader));
    return total;
}

int main(void)
{
    static const uint8_t stream[] =
        "--frame\r\nContent-Type: image/jpeg\r\n\r\n"
        "\xff\xd8\x11\xff\x00\x22\xff\xd9\r\n"
        "--frame\r\nContent-Type: image/jpeg\r\nContent-Length: 7\r\n\r\n"
        "\xff\xd8\x33\x44\x55\xff\xd9\r\n";

    fake_source_t source = {stream, sizeof(stream) - 1, 0};
    wc_mjpeg_reader_t reader;
    wc_mjpeg_reader_init(&reader, fake_read, &source, 64);

    uint8_t frame[32];
    size_t n = read_one(&reader, frame, sizeof(frame));
    static const uint8_t expected1[] = {0xff,0xd8,0x11,0xff,0x00,0x22,0xff,0xd9};
    assert(n == sizeof(expected1));
    assert(memcmp(frame, expected1, n) == 0);

    n = read_one(&reader, frame, sizeof(frame));
    static const uint8_t expected2[] = {0xff,0xd8,0x33,0x44,0x55,0xff,0xd9};
    assert(n == sizeof(expected2));
    assert(memcmp(frame, expected2, n) == 0);
    assert(!wc_mjpeg_frame_overflow(&reader));

    return 0;
}
