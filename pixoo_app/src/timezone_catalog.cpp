#include "timezone_catalog.h"

#include <cstring>

namespace pixoo {

namespace {

struct TimezoneEntry {
  const char *label;
  const char *posix;
};

#define PIXOO_TIMEZONE(label, posix) {label, posix},
#define PIXOO_DEFAULT_TIMEZONE(label)
constexpr TimezoneEntry kZones[] = {
#include "timezone_catalog.inc"
};

#undef PIXOO_TIMEZONE
#undef PIXOO_DEFAULT_TIMEZONE

constexpr size_t kZoneCount = sizeof(kZones) / sizeof(kZones[0]);
static_assert(kZoneCount == 44, "Timezone indexes are persisted");
#define PIXOO_TIMEZONE(label, posix)
#define PIXOO_DEFAULT_TIMEZONE(label) constexpr char kDefaultLabel[] = label;
#include "timezone_catalog.inc"
#undef PIXOO_TIMEZONE
#undef PIXOO_DEFAULT_TIMEZONE

}  // namespace

size_t TimezoneCount() { return kZoneCount; }

const char *TimezoneLabel(size_t index) {
  return index < kZoneCount ? kZones[index].label : nullptr;
}

const char *TimezonePosix(size_t index) {
  return index < kZoneCount ? kZones[index].posix : nullptr;
}

size_t DefaultTimezoneIndex() {
  size_t index = 0;
  TimezoneIndexForLabel(kDefaultLabel, &index);
  return index;
}

bool TimezoneIndexForLabel(const char *label, size_t *index) {
  if (label == nullptr) {
    return false;
  }
  for (size_t i = 0; i < kZoneCount; i++) {
    if (std::strcmp(label, kZones[i].label) == 0) {
      if (index != nullptr) {
        *index = i;
      }
      return true;
    }
  }
  return false;
}

}  // namespace pixoo
