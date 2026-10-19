#pragma once

#include <cstddef>
#include <cstring>

namespace pixoo {

struct DashboardRestore {
  char id[32]{};
};

inline const char *LegacyDashboardId(size_t index, const char *const *options,
                                     size_t count) {
  return options != nullptr && index < count ? options[index] : nullptr;
}

inline size_t RestoreDashboardIndex(const char *id, const char *const *options,
                                    size_t count, size_t fallback) {
  if (id != nullptr) {
    for (size_t i = 0; i < count; ++i)
      if (std::strcmp(id, options[i]) == 0) return i;
  }
  return fallback;
}

inline DashboardRestore SaveDashboardId(const char *id) {
  DashboardRestore result;
  if (id != nullptr) std::strncpy(result.id, id, sizeof(result.id) - 1);
  return result;
}

}  // namespace pixoo
