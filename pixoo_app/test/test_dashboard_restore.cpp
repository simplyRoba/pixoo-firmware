#include <unity.h>

#include <limits>

#include "dashboard_restore.h"

namespace {
constexpr const char *kOptions[] = {
    "text", "now_playing", "weather_forecast", "weather_landscape",
    "equalizer_bars", "equalizer_waveform", "game_of_life", "clock_split_flap",
    "clock_analog", "clock_binary", "clock_digital", "ddp", "clock_shadow"};

void LegacyIndexes() {
  const char *legacy[] = {
      "text", "now_playing", "weather_forecast", "weather_landscape",
      "equalizer_bars", "equalizer_waveform", "game_of_life", "clock_split_flap",
      "clock_analog", "clock_binary", "clock_digital", "stopwatch", "timer",
      "ddp", "clock_shadow"};
  for (size_t old = 0; old < 15; ++old) {
    const size_t expected = old < 11 ? old : old < 13 ? 8 : old - 2;
    TEST_ASSERT_EQUAL(expected, pixoo::RestoreDashboardIndex(
        pixoo::LegacyDashboardId(old, legacy, 15), kOptions, 13, 8));
  }
  TEST_ASSERT_EQUAL(8, pixoo::RestoreDashboardIndex(
      pixoo::LegacyDashboardId(15, legacy, 15), kOptions, 13, 8));
  TEST_ASSERT_EQUAL(8, pixoo::RestoreDashboardIndex(
      pixoo::LegacyDashboardId(std::numeric_limits<size_t>::max(), legacy, 15), kOptions, 13, 8));
}

void CustomLegacyAndNoMapping() {
  const char *legacy[] = {"ddp", "text"};
  TEST_ASSERT_EQUAL(11, pixoo::RestoreDashboardIndex(
      pixoo::LegacyDashboardId(0, legacy, 2), kOptions, 13, 8));
  TEST_ASSERT_EQUAL(8, pixoo::RestoreDashboardIndex(
      pixoo::LegacyDashboardId(0, nullptr, 0), kOptions, 13, 8));
  const auto fallback = pixoo::SaveDashboardId(kOptions[8]);
  TEST_ASSERT_EQUAL_STRING("clock_analog", fallback.id);
  const auto migrated = pixoo::SaveDashboardId(kOptions[11]);
  TEST_ASSERT_EQUAL_STRING("ddp", migrated.id);
  // A canonical record resolves by ID, not by either catalog's old index.
  TEST_ASSERT_EQUAL(11, pixoo::RestoreDashboardIndex(migrated.id, kOptions, 13, 8));
}

void CanonicalIdSurvivesReorder() {
  const char *reordered[] = {"clock_shadow", "ddp", "clock_analog", "text"};
  const auto stored = pixoo::SaveDashboardId("ddp");
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
