#pragma once
#include <cstdlib>
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_INTERNAL 2
#define MALLOC_CAP_8BIT 4
extern bool fail_spiram_allocation;
inline void *heap_caps_malloc(size_t n, unsigned caps) {
  if ((caps & MALLOC_CAP_SPIRAM) && fail_spiram_allocation) return nullptr;
  return std::malloc(n);
}
inline void heap_caps_free(void *p) { std::free(p); }
