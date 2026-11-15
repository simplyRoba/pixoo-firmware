#pragma once
#include <atomic>
#include <cstdlib>
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_INTERNAL 2
#define MALLOC_CAP_8BIT 4
extern bool fail_spiram_allocation;
inline std::atomic<size_t> heap_allocation_attempts{0};
inline std::atomic<size_t> heap_frees{0};
inline std::atomic<unsigned> last_heap_caps{0};
inline void *heap_caps_malloc(size_t n, unsigned caps) {
  ++heap_allocation_attempts;
  last_heap_caps.store(caps);
  if ((caps & MALLOC_CAP_SPIRAM) && fail_spiram_allocation) return nullptr;
  return std::malloc(n);
}
inline void heap_caps_free(void *p) {
  if (p != nullptr) ++heap_frees;
  std::free(p);
}
