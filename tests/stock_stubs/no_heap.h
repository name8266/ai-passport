#pragma once
#include <stdlib.h>
// This module reserves its arena before TLS and must not allocate/free response or JSON nodes.
void *stock_forbidden_malloc(size_t)
    __attribute__((error("stock network heap allocation forbidden")));
void *stock_forbidden_calloc(size_t, size_t)
    __attribute__((error("stock network heap allocation forbidden")));
void *stock_forbidden_realloc(void *, size_t)
    __attribute__((error("stock network heap allocation forbidden")));
void stock_forbidden_free(void *) __attribute__((error("stock network heap allocation forbidden")));
#define malloc stock_forbidden_malloc
#define calloc stock_forbidden_calloc
#define realloc stock_forbidden_realloc
#define free stock_forbidden_free
