#pragma once
#include <cstdlib>
#define MALLOC_CAP_DMA 1
#define MALLOC_CAP_INTERNAL 2
#define MALLOC_CAP_8BIT 4
inline void *heap_caps_malloc(size_t n, unsigned) { return malloc(n); }
inline void *heap_caps_calloc(size_t n, size_t s, unsigned) { return calloc(n,s); }
void heap_caps_free(void *);
