#include "stock_preview.h"
#include <limits.h>
#include <stdlib.h>
#include <string.h>

unsigned stock_list_move(unsigned selected, unsigned watch_count, int direction) {
    unsigned count = watch_count + 1;
    if (selected >= count)
        selected = watch_count;
    return direction < 0 ? (selected ? selected - 1 : watch_count)
                         : (selected + 1) % count;
}
unsigned stock_list_first(unsigned selected, unsigned count, unsigned first) {
    if (count <= STOCK_LIST_VISIBLE)
        return 0;
    if (selected >= count)
        selected = count - 1;
    if (first > count - STOCK_LIST_VISIBLE)
        first = count - STOCK_LIST_VISIBLE;
    if (selected < first)
        first = selected;
    if (selected >= first + STOCK_LIST_VISIBLE)
        first = selected - STOCK_LIST_VISIBLE + 1;
    return first;
}
stock_scroll_plan_t stock_scroll_plan(int32_t current_y, unsigned desired_first,
                                     unsigned count, bool snap) {
    unsigned last = count > STOCK_LIST_VISIBLE ? count - STOCK_LIST_VISIBLE : 0;
    if (desired_first > last)
        desired_first = last;
    int32_t maximum = (int32_t)(last * STOCK_LIST_PITCH);
    if (current_y < 0)
        current_y = 0;
    if (current_y > maximum)
        current_y = maximum;
    if (snap)
        return (stock_scroll_plan_t){desired_first, desired_first * STOCK_LIST_PITCH, false};
    unsigned floor = (unsigned)current_y / STOCK_LIST_PITCH;
    unsigned ceil = ((unsigned)current_y + STOCK_LIST_PITCH - 1) / STOCK_LIST_PITCH;
    unsigned target = desired_first;
    if (target > floor + 2)
        target = floor + 2;
    if (ceil > 2 && target < ceil - 2)
        target = ceil - 2;
    int32_t y = (int32_t)(target * STOCK_LIST_PITCH);
    return (stock_scroll_plan_t){target < floor ? target : floor, y, current_y != y};
}
unsigned stock_scroll_duration(unsigned distance) {
    unsigned extra = distance < 2 * STOCK_LIST_PITCH ? distance * 24 / STOCK_LIST_PITCH : 48;
    return 120 + (extra < 48 ? extra : 48);
}
bool stock_preview_matches(const stock_preview_t *p, const stock_quote_t *q) {
    return p->count && q->valid && !strcmp(p->code, q->code) &&
           !strncmp(p->date, q->stamp, 4) && !strncmp(p->date + 5, q->stamp + 4, 2) &&
           !strncmp(p->date + 8, q->stamp + 6, 2);
}
bool stock_preview_build(stock_preview_t *out, const stock_quote_t *q,
                         const stock_minute_t *minutes, unsigned count, const char *date) {
    if (!q->valid || q->previous <= 0 || !count || count > STOCK_MINUTES || strlen(date) != 10)
        return false;
    stock_preview_t p = {.previous = q->previous};
    memcpy(p.code, q->code, sizeof(p.code));
    memcpy(p.date, date, sizeof(p.date));
    unsigned last_slot = 0;
    for (unsigned i = 0; i < count; i++) {
        unsigned hhmm = minutes[i].hhmm, m = hhmm / 100 * 60 + hhmm % 100;
        if (hhmm % 100 >= 60 || hhmm < 930 || hhmm > 1500 ||
            (hhmm > 1130 && hhmm < 1300) || minutes[i].price <= 0)
            return false;
        unsigned slot = hhmm <= 1130 ? m - 570 : 121 + m - 780;
        if (i && slot <= last_slot)
            return false;
        if (!p.count || slot / 4 != last_slot / 4)
            p.count++;
        p.slot[p.count - 1] = (uint8_t)slot;
        p.price[p.count - 1] = minutes[i].price;
        last_slot = slot;
    }
    if (!stock_preview_matches(&p, q))
        return false;
    *out = p;
    return true;
}
static void pixel(uint8_t *p, int x, int y) {
    if (x >= 0 && x < STOCK_SPARK_WIDTH && y >= 0 && y < STOCK_SPARK_HEIGHT)
        p[y * STOCK_SPARK_STRIDE + x / 8] |= (uint8_t)(0x80u >> (x % 8));
}
static void line(uint8_t *p, int x, int y, int nx, int ny) {
    int dx = abs(nx - x), sx = x < nx ? 1 : -1;
    int dy = -abs(ny - y), sy = y < ny ? 1 : -1, error = dx + dy;
    for (;;) {
        pixel(p, x, y);
        if (x == nx && y == ny)
            return;
        int twice = 2 * error;
        if (twice >= dy) {
            error += dy;
            x += sx;
        }
        if (twice <= dx) {
            error += dx;
            y += sy;
        }
    }
}
void stock_preview_raster(const stock_preview_t *p, uint8_t pixels[STOCK_SPARK_BYTES]) {
    memset(pixels, 0, STOCK_SPARK_BYTES);
    if (!p->count || p->count > STOCK_SPARK_POINTS)
        return;
    int64_t spread = 10;
    for (unsigned i = 0; i < p->count; i++) {
        int64_t delta = (int64_t)p->price[i] - p->previous;
        if (delta < 0)
            delta = -delta;
        if (delta > spread)
            spread = delta;
    }
    int x = 0, y = 0;
    for (unsigned i = 0; i < p->count; i++) {
        int nx = p->slot[i] * (STOCK_SPARK_WIDTH - 1) / 241;
        int ny = (int)((spread - ((int64_t)p->price[i] - p->previous)) *
                       (STOCK_SPARK_HEIGHT - 1) / (spread * 2));
        if (i)
            line(pixels, x, y, nx, ny);
        else
            pixel(pixels, nx, ny);
        x = nx;
        y = ny;
    }
    pixel(pixels, x - 1, y);
    pixel(pixels, x, y - 1);
    pixel(pixels, x, y + 1);
}
