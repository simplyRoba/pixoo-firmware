#include <unity.h>

#include <limits>

#include "dashboard_restore.h"

namespace {
constexpr const char *kOptions[] = {
    "clock_analog", "clock_digital", "clock_split_flap", "clock_binary",
    "clock_shadow", "now_playing", "equalizer_bars", "equalizer_waveform",
    "weather_forecast", "weather_landscape", "text", "game_of_life", "ddp"};

void LegacyIndexes() {
  const char *legacy[] = {
      "text", "now_playing", "weather_forecast", "weather_landscape",
      "equalizer_bars", "equalizer_waveform", "game_of_life", "clock_split_flap",
      "clock_analog", "clock_binary", "clock_digital", "stopwatch", "timer",
      "ddp", "clock_shadow"};
  const size_t expected[] = {10, 5, 8, 9, 6, 7, 11, 2, 0, 3, 1, 0, 0, 12, 4};
  for (size_t old = 0; old < 15; ++old) {
    TEST_ASSERT_EQUAL(expected[old], pixoo::RestoreDashboardIndex(
        pixoo::LegacyDashboardId(old, legacy, 15), kOptions, 13, 0));
  }
  TEST_ASSERT_EQUAL(0, pixoo::RestoreDashboardIndex(
      pixoo::LegacyDashboardId(15, legacy, 15), kOptions, 13, 0));
  TEST_ASSERT_EQUAL(0, pixoo::RestoreDashboardIndex(
      pixoo::LegacyDashboardId(std::numeric_limits<size_t>::max(), legacy, 15), kOptions, 13, 0));
}

void CustomLegacyAndNoMapping() {
  const char *legacy[] = {"ddp", "text"};
  TEST_ASSERT_EQUAL(12, pixoo::RestoreDashboardIndex(
      pixoo::LegacyDashboardId(0, legacy, 2), kOptions, 13, 0));
  TEST_ASSERT_EQUAL(0, pixoo::RestoreDashboardIndex(
      pixoo::LegacyDashboardId(0, nullptr, 0), kOptions, 13, 0));
  const auto fallback = pixoo::SaveDashboardId(kOptions[0]);
  TEST_ASSERT_EQUAL_STRING("clock_analog", fallback.id);
  const auto migrated = pixoo::SaveDashboardId(kOptions[12]);
  TEST_ASSERT_EQUAL_STRING("ddp", migrated.id);
  // A canonical record resolves by ID, not by either catalog's old index.
  TEST_ASSERT_EQUAL(12, pixoo::RestoreDashboardIndex(migrated.id, kOptions, 13, 0));
}

void CanonicalIdSurvivesReorder() {
  const char *reordered[] = {"clock_shadow", "ddp", "clock_analog", "text"};
  const auto stored = pixoo::SaveDashboardId("ddp");
  TEST_ASSERT_EQUAL(12, pixoo::RestoreDashboardIndex(stored.id, kOptions, 13, 0));
  TEST_ASSERT_EQUAL(4, pixoo::RestoreDashboardIndex("clock_shadow", kOptions, 13, 0));
  TEST_ASSERT_EQUAL(1, pixoo::RestoreDashboardIndex(stored.id, reordered, 4, 2));
  TEST_ASSERT_EQUAL(0, pixoo::RestoreDashboardIndex("clock_shadow", reordered, 4, 2));
  TEST_ASSERT_EQUAL(2, pixoo::RestoreDashboardIndex("stopwatch", reordered, 4, 2));
  TEST_ASSERT_EQUAL(2, pixoo::RestoreDashboardIndex("missing", reordered, 4, 2));
  TEST_ASSERT_EQUAL(2, pixoo::RestoreDashboardIndex(nullptr, reordered, 4, 2));
  TEST_ASSERT_EQUAL_STRING("ddp", stored.id);
  TEST_ASSERT_EQUAL(32, sizeof(stored.id));
  const char *utf8 = "ééééééééééééééé";
  TEST_ASSERT_EQUAL_STRING(utf8, pixoo::SaveDashboardId(utf8).id);
  const char *max_length = "1234567890123456789012345678901";
  TEST_ASSERT_EQUAL_STRING(max_length, pixoo::SaveDashboardId(max_length).id);
}
}  // namespace

void RunDashboardRestoreTests() {
  RUN_TEST(LegacyIndexes);
  RUN_TEST(CustomLegacyAndNoMapping);
  RUN_TEST(CanonicalIdSurvivesReorder);
}
