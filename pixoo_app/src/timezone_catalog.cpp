#include "timezone_catalog.h"

#include <cstring>

namespace pixoo {

namespace {

struct TimezoneEntry {
  const char *label;
  const char *posix;
};

constexpr TimezoneEntry kZones[] = {
    {"Midway (UTC-11)", "SST11"},
    {"Honolulu (UTC-10)", "HST10"},
    {"Anchorage (UTC-9)", "AKST9AKDT,M3.2.0,M11.1.0"},
    {"Los Angeles (UTC-8)", "PST8PDT,M3.2.0,M11.1.0"},
    {"Denver (UTC-7)", "MST7MDT,M3.2.0,M11.1.0"},
    {"Phoenix (UTC-7 no DST)", "MST7"},
    {"Chicago (UTC-6)", "CST6CDT,M3.2.0,M11.1.0"},
    {"Mexico City (UTC-6)", "CST6"},
    {"New York (UTC-5)", "EST5EDT,M3.2.0,M11.1.0"},
    {"Bogota (UTC-5 no DST)", "<-05>5"},
    {"Santiago (UTC-4)", "<-04>4<-03>,M9.1.6/24,M4.1.6/24"},
    {"Halifax (UTC-4)", "AST4ADT,M3.2.0,M11.1.0"},
    {"Sao Paulo (UTC-3)", "<-03>3"},
    {"Buenos Aires (UTC-3)", "<-03>3"},
    {"South Georgia (UTC-2)", "<-02>2"},
    {"Azores (UTC-1)", "<-01>1<+00>,M3.5.0/0,M10.5.0/1"},
    {"London (UTC+0)", "GMT0BST,M3.5.0/1,M10.5.0"},
    {"UTC", "UTC0"},
    {"Berlin (UTC+1)", "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"Lagos (UTC+1 no DST)", "WAT-1"},
    {"Cairo (UTC+2)", "EET-2EEST,M4.5.5/0,M10.5.4/24"},
    {"Athens (UTC+2)", "EET-2EEST,M3.5.0/3,M10.5.0/4"},
    {"Moscow (UTC+3)", "MSK-3"},
    {"Istanbul (UTC+3)", "<+03>-3"},
    {"Tehran (UTC+3:30)", "<+0330>-3:30"},
    {"Dubai (UTC+4)", "<+04>-4"},
    {"Kabul (UTC+4:30)", "<+0430>-4:30"},
    {"Karachi (UTC+5)", "PKT-5"},
    {"Kolkata (UTC+5:30)", "IST-5:30"},
    {"Kathmandu (UTC+5:45)", "<+0545>-5:45"},
    {"Dhaka (UTC+6)", "<+06>-6"},
    {"Yangon (UTC+6:30)", "<+0630>-6:30"},
    {"Bangkok (UTC+7)", "<+07>-7"},
    {"Shanghai (UTC+8)", "CST-8"},
    {"Singapore (UTC+8)", "<+08>-8"},
    {"Tokyo (UTC+9)", "JST-9"},
    {"Seoul (UTC+9)", "KST-9"},
    {"Adelaide (UTC+9:30)", "ACST-9:30ACDT,M10.1.0,M4.1.0/3"},
    {"Sydney (UTC+10)", "AEST-10AEDT,M10.1.0,M4.1.0/3"},
    {"Brisbane (UTC+10 no DST)", "AEST-10"},
    {"Noumea (UTC+11)", "<+11>-11"},
    {"Auckland (UTC+12)", "NZST-12NZDT,M9.5.0,M4.1.0/3"},
    {"Tongatapu (UTC+13)", "<+13>-13"},
    {"Kiritimati (UTC+14)", "<+14>-14"},
};

constexpr size_t kZoneCount = sizeof(kZones) / sizeof(kZones[0]);
constexpr char kDefaultLabel[] = "New York (UTC-5)";

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
