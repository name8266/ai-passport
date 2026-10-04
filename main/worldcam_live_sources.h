#pragma once
#include <stddef.h>

typedef struct {
    const char *name;
    const char *area;
    const char *category;
    const char *url;
    const char *source_page;
} wc_live_source_t;

extern const wc_live_source_t wc_live_sources[];
extern const size_t wc_live_source_count;
