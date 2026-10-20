#pragma once
#include "brake_render.h"
enum { BH_EV_START = 64 };
typedef struct {
    bh_view_t view;
    bh_clock_t clock;
    int64_t page_since, input_after;
    uint32_t held_mask, session, finished_session;
} bh_app_t;
void bh_app_init(bh_app_t *a, int64_t now);
uint32_t bh_app_tick(bh_app_t *a, int64_t now);
uint32_t bh_app_input(bh_app_t *a, bh_key_t key, bool pressed, int64_t now);
uint32_t bh_app_sync(bh_app_t *a, uint32_t mask, int64_t now);
