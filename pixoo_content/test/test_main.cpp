#include <unity.h>

#include <cmath>
#include <cstring>
#include <ctime>
#include <vector>

#include "analog_clock.h"
#include "binary_clock.h"
#include "open_meteo_url.h"
#include "refresh_policy.h"
#include "sky_astronomy.h"
#include "split_flap.h"
#include "weather_data.h"
#include "weather_effects.h"
#include "wmo.h"
#include "equalizer_processor.h"
#include "game_of_life.h"
#include "spectrum.h"

using namespace pixoo;
using pixoo::life::GameOfLifeModel;

void setUp() {}
void tearDown() {}

static WeatherData make_series(int start_hour, int count) {
  WeatherData d{};
  d.hour_count = count;
  for (int i = 0; i < count; i++) {
    d.hours[i].valid = true;
    d.hours[i].hour_of_day = (start_hour + i) % 24;
    d.hours[i].temperature = 10.0f + i;
  }
  return d;
}

static void test_display_degrees_rounds_to_nearest_without_signed_zero() {
  // A reading is never carried up to the next degree.
  TEST_ASSERT_EQUAL_INT(31, DisplayDegrees(31.1f));
  TEST_ASSERT_EQUAL_INT(31, DisplayDegrees(31.4f));
  TEST_ASSERT_EQUAL_INT(31, DisplayDegrees(30.6f));
  TEST_ASSERT_EQUAL_INT(-3, DisplayDegrees(-3.2f));

  // Halves go away from zero, so neighbouring values cannot land on the same
  // degree the way round-half-to-even would.
  TEST_ASSERT_EQUAL_INT(32, DisplayDegrees(31.5f));
  TEST_ASSERT_EQUAL_INT(33, DisplayDegrees(32.5f));
  TEST_ASSERT_EQUAL_INT(-4, DisplayDegrees(-3.5f));
  TEST_ASSERT_EQUAL_INT(-3, DisplayDegrees(-2.5f));

  // Just below zero resolves to plain zero rather than a signed zero.
  TEST_ASSERT_EQUAL_INT(0, DisplayDegrees(-0.4f));
  TEST_ASSERT_EQUAL_INT(0, DisplayDegrees(-0.0f));
  TEST_ASSERT_EQUAL_INT(0, DisplayDegrees(0.4f));
  TEST_ASSERT_EQUAL_INT(-1, DisplayDegrees(-0.5f));
}

static void test_select_now_matches_current_hour() {
  // Series fetched at 13:00; wall clock is now 15:00.
  const WeatherData d = make_series(13, 8);
  const WeatherNow n = SelectWeatherNow(d, 15);
  TEST_ASSERT_FALSE(n.current_is_fetch_hour);
  TEST_ASSERT_TRUE(n.has_current_hour);
  TEST_ASSERT_EQUAL(15, n.current.hour_of_day);
  TEST_ASSERT_EQUAL_FLOAT(12.0f, n.current.temperature);  // index 2
  // Columns are the next hours after 15: 16, 17, 18.
  TEST_ASSERT_EQUAL(16, n.columns[0].hour_of_day);
  TEST_ASSERT_EQUAL(17, n.columns[1].hour_of_day);
  TEST_ASSERT_EQUAL(18, n.columns[2].hour_of_day);
}

static void test_select_now_wraps_past_midnight() {
  const WeatherData d = make_series(22, 6);  // 22,23,0,1,2,3
  const WeatherNow n = SelectWeatherNow(d, 23);
  TEST_ASSERT_TRUE(n.has_current_hour);
  TEST_ASSERT_EQUAL(23, n.current.hour_of_day);
  TEST_ASSERT_EQUAL(0, n.columns[0].hour_of_day);
  TEST_ASSERT_EQUAL(1, n.columns[1].hour_of_day);
  TEST_ASSERT_EQUAL(2, n.columns[2].hour_of_day);
}

static void test_select_now_flags_the_fetch_hour() {
  // The clock has not left the hour the snapshot was fetched in, so the live
  // readings that came with it still describe the current hour.
  const WeatherData d = make_series(13, 8);
  const WeatherNow n = SelectWeatherNow(d, 13);
  TEST_ASSERT_TRUE(n.has_current_hour);
  TEST_ASSERT_TRUE(n.current_is_fetch_hour);
  TEST_ASSERT_EQUAL(14, n.columns[0].hour_of_day);

  // One hour on, the series entry is the one that describes the hour.
  const WeatherNow later = SelectWeatherNow(d, 14);
  TEST_ASSERT_TRUE(later.has_current_hour);
  TEST_ASSERT_FALSE(later.current_is_fetch_hour);
}

static void test_select_now_falls_back_without_clock() {
  const WeatherData d = make_series(13, 8);
  const WeatherNow n = SelectWeatherNow(d, -1);
  TEST_ASSERT_FALSE(n.has_current_hour);
  TEST_ASSERT_FALSE(n.current_is_fetch_hour);
  // Columns fall back to the first entries of the series.
  TEST_ASSERT_EQUAL(13, n.columns[0].hour_of_day);
  TEST_ASSERT_EQUAL(14, n.columns[1].hour_of_day);
  TEST_ASSERT_EQUAL(15, n.columns[2].hour_of_day);
}

static void test_select_now_hour_absent_from_series() {
  const WeatherData d = make_series(13, 8);  // covers 13..20
  const WeatherNow n = SelectWeatherNow(d, 5);
  TEST_ASSERT_FALSE(n.has_current_hour);
  TEST_ASSERT_EQUAL(13, n.columns[0].hour_of_day);
}

static void test_select_now_truncates_at_series_end() {
  const WeatherData d = make_series(13, 4);  // 13,14,15,16
  const WeatherNow n = SelectWeatherNow(d, 15);
  TEST_ASSERT_TRUE(n.has_current_hour);
  TEST_ASSERT_TRUE(n.columns[0].valid);   // 16
  TEST_ASSERT_FALSE(n.columns[1].valid);  // past end
  TEST_ASSERT_FALSE(n.columns[2].valid);
}

static void test_open_meteo_url_includes_complete_forecast_query() {
  const std::string url = BuildOpenMeteoForecastUrl(52.5200f, 13.4050f);
  TEST_ASSERT_EQUAL_STRING(
      "https://api.open-meteo.com/v1/forecast?latitude=52.5200&longitude=13.4050"
      "&current=temperature_2m,relative_humidity_2m,apparent_temperature,"
      "weather_code,is_day"
      "&hourly=temperature_2m,weather_code,apparent_temperature,"
      "relative_humidity_2m,is_day"
      "&daily=temperature_2m_max,temperature_2m_min"
      "&forecast_days=2&forecast_hours=12&timezone=auto",
      url.c_str());
}

static void test_open_meteo_url_handles_widest_coordinates() {
  const std::string url = BuildOpenMeteoForecastUrl(-90.0f, -180.0f);
  TEST_ASSERT_NOT_NULL(strstr(url.c_str(), "latitude=-90.0000&longitude=-180.0000"));
  TEST_ASSERT_NOT_NULL(strstr(url.c_str(), "&forecast_hours=12&timezone=auto"));
}

static void test_refresh_policy_requests_initially() {
  WeatherRefreshPolicy policy;
  uint32_t generation = 99;
  TEST_ASSERT_TRUE(policy.BeginRequest(0, 60000, &generation));
  TEST_ASSERT_EQUAL_UINT32(0, generation);
  TEST_ASSERT_FALSE(policy.BeginRequest(0, 60000, nullptr));
}

static void test_refresh_policy_keeps_success_fresh_until_stale() {
  WeatherRefreshPolicy policy;
  uint32_t generation;
  TEST_ASSERT_TRUE(policy.BeginRequest(100, 60000, &generation));
  TEST_ASSERT_TRUE(policy.Complete(generation, true, 100) ==
                   WeatherRefreshPolicy::Completion::kSuccess);
  TEST_ASSERT_TRUE(policy.HasCurrentData());
  TEST_ASSERT_FALSE(policy.BeginRequest(60099, 60000, nullptr));
  TEST_ASSERT_TRUE(policy.BeginRequest(60100, 60000, nullptr));
}

static void test_refresh_policy_refreshes_when_success_becomes_stale() {
  WeatherRefreshPolicy policy;
  uint32_t generation;
  TEST_ASSERT_TRUE(policy.BeginRequest(0, 100, &generation));
  TEST_ASSERT_TRUE(policy.Complete(generation, true, 0) ==
                   WeatherRefreshPolicy::Completion::kSuccess);
  TEST_ASSERT_TRUE(policy.BeginRequest(100, 100, &generation));
  TEST_ASSERT_TRUE(policy.Complete(generation, true, 100) ==
                   WeatherRefreshPolicy::Completion::kSuccess);
  TEST_ASSERT_FALSE(policy.BeginRequest(101, 100, nullptr));
}

static void test_refresh_policy_preserves_same_location_data_after_failure() {
  WeatherRefreshPolicy policy;
  uint32_t generation;
  TEST_ASSERT_TRUE(policy.BeginRequest(0, 100, &generation));
  TEST_ASSERT_TRUE(policy.Complete(generation, true, 0) ==
                   WeatherRefreshPolicy::Completion::kSuccess);
  TEST_ASSERT_TRUE(policy.BeginRequest(100, 100, &generation));
  TEST_ASSERT_TRUE(policy.Complete(generation, false, 100) ==
                   WeatherRefreshPolicy::Completion::kFailure);
  TEST_ASSERT_TRUE(policy.HasCurrentData());
}

static void test_refresh_policy_retries_failure_after_backoff() {
  WeatherRefreshPolicy policy;
  uint32_t generation;
  TEST_ASSERT_TRUE(policy.BeginRequest(10, 60000, &generation));
  TEST_ASSERT_TRUE(policy.Complete(generation, false, 10) ==
                   WeatherRefreshPolicy::Completion::kFailure);
  TEST_ASSERT_FALSE(policy.BeginRequest(30009, 60000, nullptr));
  TEST_ASSERT_TRUE(policy.BeginRequest(30010, 60000, nullptr));
}

static void test_refresh_policy_forced_invalidation_hides_old_data() {
  WeatherRefreshPolicy policy;
  uint32_t old_generation;
  TEST_ASSERT_TRUE(policy.BeginRequest(0, 60000, &old_generation));
  TEST_ASSERT_TRUE(policy.Complete(old_generation, true, 0) ==
                   WeatherRefreshPolicy::Completion::kSuccess);
  TEST_ASSERT_TRUE(policy.HasCurrentData());
  policy.Invalidate();
  TEST_ASSERT_FALSE(policy.HasCurrentData());
  uint32_t generation;
  TEST_ASSERT_TRUE(policy.BeginRequest(1, 60000, &generation));
  TEST_ASSERT_EQUAL_UINT32(old_generation + 1, generation);
  TEST_ASSERT_TRUE(policy.Complete(generation, false, 1) ==
                   WeatherRefreshPolicy::Completion::kFailure);
  TEST_ASSERT_FALSE(policy.HasCurrentData());
}

static void test_refresh_policy_rejects_old_inflight_completion() {
  WeatherRefreshPolicy policy;
  uint32_t old_generation;
  TEST_ASSERT_TRUE(policy.BeginRequest(0, 60000, &old_generation));
  policy.Invalidate();
  TEST_ASSERT_TRUE(policy.Complete(old_generation, true, 1) ==
                   WeatherRefreshPolicy::Completion::kIgnored);
  TEST_ASSERT_FALSE(policy.HasCurrentData());
  uint32_t generation;
  TEST_ASSERT_TRUE(policy.BeginRequest(1, 60000, &generation));
  TEST_ASSERT_EQUAL_UINT32(old_generation + 1, generation);
}

static void test_refresh_policy_handles_millis_wraparound() {
  WeatherRefreshPolicy policy;
  uint32_t generation;
  TEST_ASSERT_TRUE(policy.BeginRequest(0xfffffff0U, 64, &generation));
  TEST_ASSERT_TRUE(policy.Complete(generation, true, 0xfffffff0U) ==
                   WeatherRefreshPolicy::Completion::kSuccess);
  TEST_ASSERT_FALSE(policy.BeginRequest(0x00000020U, 64, nullptr));
  TEST_ASSERT_TRUE(policy.BeginRequest(0x00000030U, 64, nullptr));
}

static void test_wmo_clear_maps_without_time_of_day() {
  TEST_ASSERT_TRUE(WmoToCondition(0) == WeatherCondition::SUNNY);
  TEST_ASSERT_TRUE(WmoToCondition(1) == WeatherCondition::SUNNY);
}

static void test_wmo_maps_each_group() {
  TEST_ASSERT_TRUE(WmoToCondition(2) == WeatherCondition::PARTLYCLOUDY);
  TEST_ASSERT_TRUE(WmoToCondition(3) == WeatherCondition::CLOUDY);
  TEST_ASSERT_TRUE(WmoToCondition(45) == WeatherCondition::FOG);
  TEST_ASSERT_TRUE(WmoToCondition(48) == WeatherCondition::FOG);
  TEST_ASSERT_TRUE(WmoToCondition(51) == WeatherCondition::DRIZZLE);
  TEST_ASSERT_TRUE(WmoToCondition(55) == WeatherCondition::DRIZZLE);
  TEST_ASSERT_TRUE(WmoToCondition(56) ==
                   WeatherCondition::FREEZING_DRIZZLE);
  TEST_ASSERT_TRUE(WmoToCondition(61) == WeatherCondition::RAINY);
  TEST_ASSERT_TRUE(WmoToCondition(80) == WeatherCondition::RAINY);
  TEST_ASSERT_TRUE(WmoToCondition(65) == WeatherCondition::POURING);
  TEST_ASSERT_TRUE(WmoToCondition(82) == WeatherCondition::POURING);
  TEST_ASSERT_TRUE(WmoToCondition(66) == WeatherCondition::FREEZING_RAIN);
  TEST_ASSERT_TRUE(WmoToCondition(71) == WeatherCondition::SNOWY);
  TEST_ASSERT_TRUE(WmoToCondition(86) == WeatherCondition::SNOWY);
  TEST_ASSERT_TRUE(WmoToCondition(77) == WeatherCondition::SNOW_GRAINS);
  TEST_ASSERT_TRUE(WmoToCondition(95) == WeatherCondition::THUNDERSTORM);
  TEST_ASSERT_TRUE(WmoToCondition(96) ==
                   WeatherCondition::HAIL_THUNDERSTORM);
  TEST_ASSERT_TRUE(WmoToCondition(99) ==
                   WeatherCondition::HAIL_THUNDERSTORM);
}

static void test_wmo_unknown_code_falls_back() {
  TEST_ASSERT_TRUE(WmoToCondition(4) == WeatherCondition::UNKNOWN);
  TEST_ASSERT_TRUE(WmoToCondition(-1) == WeatherCondition::UNKNOWN);
  TEST_ASSERT_TRUE(WmoToCondition(100) == WeatherCondition::UNKNOWN);
}

static void test_weather_effect_maps_every_condition() {
  struct Expected {
    WeatherCondition condition;
    CloudCover clouds;
    PrecipitationKind precipitation;
    PrecipitationIntensity intensity;
    bool fog;
    bool freezing;
    bool storm;
  };
  constexpr Expected kExpected[] = {
      {WeatherCondition::UNKNOWN, CloudCover::NONE, PrecipitationKind::NONE,
       PrecipitationIntensity::NONE, false, false, false},
      {WeatherCondition::SUNNY, CloudCover::NONE, PrecipitationKind::NONE,
       PrecipitationIntensity::NONE, false, false, false},
      {WeatherCondition::PARTLYCLOUDY, CloudCover::PARTLY_CLOUDY,
       PrecipitationKind::NONE, PrecipitationIntensity::NONE, false, false,
       false},
      {WeatherCondition::CLOUDY, CloudCover::CLOUDY, PrecipitationKind::NONE,
       PrecipitationIntensity::NONE, false, false, false},
      {WeatherCondition::FOG, CloudCover::NONE, PrecipitationKind::NONE,
       PrecipitationIntensity::NONE, true, false, false},
      {WeatherCondition::DRIZZLE, CloudCover::OVERCAST,
       PrecipitationKind::DRIZZLE, PrecipitationIntensity::SPARSE, false,
       false, false},
      {WeatherCondition::FREEZING_DRIZZLE, CloudCover::OVERCAST,
       PrecipitationKind::DRIZZLE, PrecipitationIntensity::SPARSE, false, true,
       false},
      {WeatherCondition::RAINY, CloudCover::OVERCAST, PrecipitationKind::RAIN,
       PrecipitationIntensity::MEDIUM, false, false, false},
      {WeatherCondition::POURING, CloudCover::OVERCAST,
       PrecipitationKind::RAIN, PrecipitationIntensity::HEAVY, false, false,
       false},
      {WeatherCondition::FREEZING_RAIN, CloudCover::OVERCAST,
       PrecipitationKind::RAIN, PrecipitationIntensity::MEDIUM, false, true,
       false},
      {WeatherCondition::SNOWY, CloudCover::OVERCAST, PrecipitationKind::SNOW,
       PrecipitationIntensity::MEDIUM, false, false, false},
      {WeatherCondition::SNOW_GRAINS, CloudCover::OVERCAST,
       PrecipitationKind::SNOW_GRAINS, PrecipitationIntensity::MEDIUM, false,
       false, false},
      {WeatherCondition::THUNDERSTORM, CloudCover::OVERCAST,
       PrecipitationKind::RAIN, PrecipitationIntensity::HEAVY, false, false,
       true},
      {WeatherCondition::HAIL_THUNDERSTORM, CloudCover::OVERCAST,
       PrecipitationKind::HAIL, PrecipitationIntensity::HEAVY, false, false,
       true},
  };
  for (const Expected &expected : kExpected) {
    const WeatherEffect actual = WeatherEffectFor(expected.condition);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(expected.clouds),
                            static_cast<uint8_t>(actual.clouds));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(expected.precipitation),
                            static_cast<uint8_t>(actual.precipitation));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(expected.intensity),
                            static_cast<uint8_t>(actual.intensity));
    TEST_ASSERT_EQUAL(expected.fog, actual.fog);
    TEST_ASSERT_EQUAL(expected.freezing, actual.freezing);
    TEST_ASSERT_EQUAL(expected.storm, actual.storm);
  }
}

static void test_weather_transition_first_condition_is_settled() {
  WeatherTransitionState transition;
  transition.Update(WeatherCondition::SUNNY, 100u);

  TEST_ASSERT_TRUE(transition.initialized());
  TEST_ASSERT_FALSE(transition.transitioning());
  TEST_ASSERT_EQUAL_FLOAT(1.0f, transition.blend());
  TEST_ASSERT_TRUE(transition.outgoing_condition() == WeatherCondition::SUNNY);
  TEST_ASSERT_TRUE(transition.incoming_condition() == WeatherCondition::SUNNY);
  TEST_ASSERT_EQUAL_UINT8(
      static_cast<uint8_t>(transition.outgoing_effect().precipitation),
      static_cast<uint8_t>(PrecipitationKind::NONE));
  TEST_ASSERT_EQUAL_UINT8(
      static_cast<uint8_t>(transition.incoming_effect().precipitation),
      static_cast<uint8_t>(PrecipitationKind::NONE));
}

static void test_weather_transition_blends_and_settles() {
  WeatherTransitionState transition;
  transition.Update(WeatherCondition::SUNNY, 0u);
  transition.Update(WeatherCondition::RAINY, 100u);
  const uint64_t duration_ms = transition.duration_ms();

  TEST_ASSERT_TRUE(transition.transitioning());
  TEST_ASSERT_EQUAL_FLOAT(0.0f, transition.blend());
  TEST_ASSERT_TRUE(transition.outgoing_condition() == WeatherCondition::SUNNY);
  TEST_ASSERT_TRUE(transition.incoming_condition() == WeatherCondition::RAINY);
  TEST_ASSERT_EQUAL_UINT8(
      static_cast<uint8_t>(transition.outgoing_effect().precipitation),
      static_cast<uint8_t>(PrecipitationKind::NONE));
  TEST_ASSERT_EQUAL_UINT8(
      static_cast<uint8_t>(transition.incoming_effect().precipitation),
      static_cast<uint8_t>(PrecipitationKind::RAIN));

  transition.Update(WeatherCondition::RAINY, 100u + duration_ms / 2u);
  TEST_ASSERT_TRUE(transition.transitioning());
  TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.5f, transition.blend());

  transition.Update(WeatherCondition::RAINY, 100u + duration_ms);
  TEST_ASSERT_FALSE(transition.transitioning());
  TEST_ASSERT_EQUAL_FLOAT(1.0f, transition.blend());
  TEST_ASSERT_TRUE(transition.outgoing_condition() == WeatherCondition::RAINY);
  TEST_ASSERT_TRUE(transition.incoming_condition() == WeatherCondition::RAINY);

  transition.Update(WeatherCondition::RAINY, 100u + duration_ms + 500u);
  TEST_ASSERT_FALSE(transition.transitioning());
  TEST_ASSERT_EQUAL_FLOAT(1.0f, transition.blend());
}

static void test_weather_transition_defers_retarget_without_jump() {
  WeatherTransitionState transition;
  transition.Update(WeatherCondition::SUNNY, 0u);
  transition.Update(WeatherCondition::RAINY, 100u);
  const uint64_t duration_ms = transition.duration_ms();
  transition.Update(WeatherCondition::SNOWY, 100u + duration_ms / 2u);

  // A new target does not replace the active sunny-to-rain blend.
  TEST_ASSERT_TRUE(transition.transitioning());
  TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.5f, transition.blend());
  TEST_ASSERT_TRUE(transition.outgoing_condition() == WeatherCondition::SUNNY);
  TEST_ASSERT_TRUE(transition.incoming_condition() == WeatherCondition::RAINY);

  // The transition endpoint and the next transition's blend-zero frame are
  // both rain, so observing snow cannot create a rendered jump.
  transition.Update(WeatherCondition::SNOWY, 100u + duration_ms);
  TEST_ASSERT_TRUE(transition.transitioning());
  TEST_ASSERT_EQUAL_FLOAT(0.0f, transition.blend());
  TEST_ASSERT_TRUE(transition.outgoing_condition() == WeatherCondition::RAINY);
  TEST_ASSERT_TRUE(transition.incoming_condition() == WeatherCondition::SNOWY);
  TEST_ASSERT_EQUAL_UINT8(
      static_cast<uint8_t>(transition.outgoing_effect().precipitation),
      static_cast<uint8_t>(PrecipitationKind::RAIN));
  TEST_ASSERT_EQUAL_UINT8(
      static_cast<uint8_t>(transition.incoming_effect().precipitation),
      static_cast<uint8_t>(PrecipitationKind::SNOW));
}

static void test_weather_transition_uses_latest_deferred_condition() {
  WeatherTransitionState transition;
  transition.Update(WeatherCondition::SUNNY, 0u);
  transition.Update(WeatherCondition::RAINY, 100u);
  const uint64_t first_ms = transition.duration_ms();
  transition.Update(WeatherCondition::SNOWY, 100u + first_ms / 2u);
  transition.Update(WeatherCondition::CLOUDY, 100u + first_ms * 3u / 4u);

  // Cloudy supersedes snowy while sunny-to-rain finishes unchanged.
  TEST_ASSERT_TRUE(transition.transitioning());
  TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.75f, transition.blend());
  TEST_ASSERT_TRUE(transition.outgoing_condition() == WeatherCondition::SUNNY);
  TEST_ASSERT_TRUE(transition.incoming_condition() == WeatherCondition::RAINY);

  transition.Update(WeatherCondition::CLOUDY, 100u + first_ms);
  TEST_ASSERT_TRUE(transition.transitioning());
  TEST_ASSERT_EQUAL_FLOAT(0.0f, transition.blend());
  TEST_ASSERT_TRUE(transition.outgoing_condition() == WeatherCondition::RAINY);
  TEST_ASSERT_TRUE(transition.incoming_condition() == WeatherCondition::CLOUDY);

  const uint64_t second_ms = transition.duration_ms();
  transition.Update(WeatherCondition::CLOUDY, 100u + first_ms + second_ms);
  TEST_ASSERT_FALSE(transition.transitioning());
  TEST_ASSERT_EQUAL_FLOAT(1.0f, transition.blend());
  TEST_ASSERT_TRUE(transition.outgoing_condition() == WeatherCondition::CLOUDY);
  TEST_ASSERT_TRUE(transition.incoming_condition() == WeatherCondition::CLOUDY);

  // Repeating the settled condition is stable and starts no new handoff.
  transition.Update(WeatherCondition::CLOUDY,
                    100u + first_ms + second_ms + 500u);
  TEST_ASSERT_FALSE(transition.transitioning());
  TEST_ASSERT_EQUAL_FLOAT(1.0f, transition.blend());
}

static void test_weather_transition_duration_scales_with_changed_traits() {
  // One differing trait is the shortest handoff; every trait differing is the
  // longest, and durations never leave the declared bounds.
  TEST_ASSERT_EQUAL_UINT64(
      kWeatherTransitionMinDurationMs,
      WeatherTransitionDurationMs(WeatherCondition::SUNNY,
                                  WeatherCondition::PARTLYCLOUDY));
  TEST_ASSERT_EQUAL_UINT64(
      kWeatherTransitionMinDurationMs,
      WeatherTransitionDurationMs(WeatherCondition::RAINY,
                                  WeatherCondition::POURING));

  // Only the falling kind differs between rain and snow, so it stays brief.
  TEST_ASSERT_EQUAL_UINT64(
      kWeatherTransitionMinDurationMs,
      WeatherTransitionDurationMs(WeatherCondition::RAINY,
                                  WeatherCondition::SNOWY));

  const uint64_t rain_to_storm = WeatherTransitionDurationMs(
      WeatherCondition::RAINY, WeatherCondition::THUNDERSTORM);
  const uint64_t sunny_to_hail = WeatherTransitionDurationMs(
      WeatherCondition::SUNNY, WeatherCondition::HAIL_THUNDERSTORM);
  TEST_ASSERT_TRUE(rain_to_storm > kWeatherTransitionMinDurationMs);
  TEST_ASSERT_TRUE(sunny_to_hail > rain_to_storm);
  TEST_ASSERT_TRUE(sunny_to_hail <= kWeatherTransitionMaxDurationMs);

  // Direction does not change how long a handoff takes.
  TEST_ASSERT_EQUAL_UINT64(
      sunny_to_hail,
      WeatherTransitionDurationMs(WeatherCondition::HAIL_THUNDERSTORM,
                                  WeatherCondition::SUNNY));

  for (uint8_t from = 0; from <= static_cast<uint8_t>(
                                    WeatherCondition::HAIL_THUNDERSTORM);
       from++) {
    for (uint8_t to = 0;
         to <= static_cast<uint8_t>(WeatherCondition::HAIL_THUNDERSTORM);
         to++) {
      const uint64_t duration_ms = WeatherTransitionDurationMs(
          static_cast<WeatherCondition>(from),
          static_cast<WeatherCondition>(to));
      TEST_ASSERT_TRUE(duration_ms >= kWeatherTransitionMinDurationMs);
      TEST_ASSERT_TRUE(duration_ms <= kWeatherTransitionMaxDurationMs);
    }
  }
}

static void test_weather_mix_moves_graded_traits_continuously() {
  const WeatherEffect sunny = WeatherEffectFor(WeatherCondition::SUNNY);
  const WeatherEffect overcast = WeatherEffectFor(WeatherCondition::CLOUDY);
  const WeatherEffect fog = WeatherEffectFor(WeatherCondition::FOG);

  // Cover, fog, freezing, and storm are single moving quantities, so a scene
  // never holds two states of the same trait at once.
  TEST_ASSERT_EQUAL_FLOAT(0.0f, MixWeatherEffects(sunny, overcast, 0.0f)
                                    .cloud_cover);
  TEST_ASSERT_EQUAL_FLOAT(CloudCoverLevel(CloudCover::CLOUDY),
                          MixWeatherEffects(sunny, overcast, 1.0f).cloud_cover);
  const float midpoint =
      MixWeatherEffects(sunny, overcast, 0.5f).cloud_cover;
  TEST_ASSERT_TRUE(midpoint > 0.0f &&
                   midpoint < CloudCoverLevel(CloudCover::CLOUDY));

  TEST_ASSERT_EQUAL_FLOAT(0.0f, MixWeatherEffects(sunny, fog, 0.0f).fog);
  TEST_ASSERT_EQUAL_FLOAT(1.0f, MixWeatherEffects(sunny, fog, 1.0f).fog);
  TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.5f,
                           MixWeatherEffects(sunny, fog, 0.5f).fog);

  // The curve eases at both ends, so a handoff neither starts nor stops with a
  // visible jump.
  TEST_ASSERT_TRUE(MixWeatherEffects(sunny, fog, 0.05f).fog < 0.05f);
  TEST_ASSERT_TRUE(MixWeatherEffects(sunny, fog, 0.95f).fog > 0.95f);

  // A monotone quantity never reverses on its way across.
  float previous = 0.0f;
  for (int step = 0; step <= 100; step++) {
    const float value =
        MixWeatherEffects(sunny, overcast, step / 100.0f).cloud_cover;
    TEST_ASSERT_TRUE(value >= previous - 0.0001f);
    previous = value;
  }
}

static void test_weather_mix_hands_over_precipitation_by_field() {
  const WeatherEffect rainy = WeatherEffectFor(WeatherCondition::RAINY);
  const WeatherEffect pouring = WeatherEffectFor(WeatherCondition::POURING);
  const WeatherEffect snowy = WeatherEffectFor(WeatherCondition::SNOWY);
  const WeatherEffect sunny = WeatherEffectFor(WeatherCondition::SUNNY);

  // An unchanged field is one continuous set of particles.
  const WeatherEffectMix steady = MixWeatherEffects(rainy, rainy, 0.5f);
  TEST_ASSERT_EQUAL_UINT8(1u, steady.precipitation_count);
  TEST_ASSERT_EQUAL_FLOAT(1.0f, steady.precipitation[0].weight);

  // Different kinds cannot share particles, so each field holds its own share
  // and the two shares always account for exactly one field's worth.
  const WeatherEffectMix swap = MixWeatherEffects(rainy, snowy, 0.5f);
  TEST_ASSERT_EQUAL_UINT8(2u, swap.precipitation_count);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(PrecipitationKind::RAIN),
                          static_cast<uint8_t>(swap.precipitation[0].kind));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(PrecipitationKind::SNOW),
                          static_cast<uint8_t>(swap.precipitation[1].kind));
  TEST_ASSERT_FLOAT_WITHIN(
      0.0001f, 1.0f,
      swap.precipitation[0].weight + swap.precipitation[1].weight);

  // A cadence change is also a separate field, so no drop changes speed.
  const WeatherEffectMix faster = MixWeatherEffects(rainy, pouring, 0.5f);
  TEST_ASSERT_EQUAL_UINT8(2u, faster.precipitation_count);
  TEST_ASSERT_FLOAT_WITHIN(
      0.0001f, 1.0f,
      faster.precipitation[0].weight + faster.precipitation[1].weight);

  // The endpoints carry only the condition that owns them.
  const WeatherEffectMix start = MixWeatherEffects(sunny, rainy, 0.0f);
  TEST_ASSERT_EQUAL_UINT8(0u, start.precipitation_count);
  const WeatherEffectMix end = MixWeatherEffects(sunny, rainy, 1.0f);
  TEST_ASSERT_EQUAL_UINT8(1u, end.precipitation_count);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(PrecipitationKind::RAIN),
                          static_cast<uint8_t>(end.precipitation[0].kind));
  TEST_ASSERT_EQUAL_FLOAT(1.0f, end.precipitation[0].weight);

  // A settled condition resolves to the plain condition it maps to.
  const WeatherEffectMix settled = MixWeatherEffects(snowy, snowy, 1.0f);
  TEST_ASSERT_EQUAL_UINT8(1u, settled.precipitation_count);
  TEST_ASSERT_EQUAL_FLOAT(0.0f, settled.fog);
  TEST_ASSERT_EQUAL_FLOAT(0.0f, settled.storm);
  TEST_ASSERT_EQUAL_FLOAT(CloudCoverLevel(CloudCover::OVERCAST),
                          settled.cloud_cover);
}

static void test_weather_animation_clock_resume_rebases_without_catch_up() {
  WeatherAnimationClock clock;
  clock.Resume(100u);
  TEST_ASSERT_EQUAL_UINT64(0u, clock.elapsed_ms());
  clock.Tick(150u);
  TEST_ASSERT_EQUAL_UINT64(50u, clock.elapsed_ms());

  clock.Resume(10000u);
  TEST_ASSERT_EQUAL_UINT64(50u, clock.elapsed_ms());
  clock.Tick(10025u);
  TEST_ASSERT_EQUAL_UINT64(75u, clock.elapsed_ms());

  clock.Resume(20000u);
  TEST_ASSERT_EQUAL_UINT64(75u, clock.elapsed_ms());
  clock.Tick(20040u);
  TEST_ASSERT_EQUAL_UINT64(115u, clock.elapsed_ms());
}

static void test_weather_animation_clock_survives_millis_wraparound() {
  WeatherAnimationClock clock;
  clock.Reset(0xfffffff0u);
  clock.Tick(0x00000010u);
  TEST_ASSERT_EQUAL_UINT64(32u, clock.elapsed_ms());
  clock.Resume(0xfffffff0u);
  clock.Tick(0x00000010u);
  TEST_ASSERT_EQUAL_UINT64(64u, clock.elapsed_ms());
  TEST_ASSERT_EQUAL_UINT32(32u, ElapsedMillis(0x00000010u, 0xfffffff0u));
}

static void test_weather_animation_phase_and_hash_are_deterministic() {
  const uint32_t hash = WeatherHash(0x12345678u);
  TEST_ASSERT_EQUAL_UINT32(hash, WeatherHash(0x12345678u));
  TEST_ASSERT_NOT_EQUAL(hash, WeatherHash(0x12345679u));
  TEST_ASSERT_EQUAL_UINT32(AnimationPhase(123456u, 3072u, 19u),
                            AnimationPhase(123456u, 3072u, 19u));
  TEST_ASSERT_LESS_THAN(3072u, AnimationPhase(123456u, 3072u, 19u));
  TEST_ASSERT_EQUAL_UINT32(0u, AnimationPhase(123456u, 0u, 19u));
}

static void test_fog_density_is_bounded_smooth_and_horizontally_seamless() {
  constexpr uint32_t kWidth = 64u;
  // Bounded, deterministic, and defined everywhere the renderer samples.
  for (uint64_t t = 0; t < 90000u; t += 4321u) {
    for (int y = 0; y < 64; y += 7) {
      for (int x = 0; x < 64; x += 5) {
        const float v = FogDensityAt(x, y, t, kWidth);
        TEST_ASSERT_TRUE(v >= 0.0f && v <= 1.0f);
        TEST_ASSERT_EQUAL_FLOAT(v, FogDensityAt(x, y, t, kWidth));
      }
    }
  }

  // Seamless across the horizontal repeat: sampling one full width apart lands
  // on the same point of the field, so a drifting bank never shows a join.
  for (uint64_t t = 0; t < 90000u; t += 7919u) {
    for (int y = 0; y < 64; y += 9) {
      for (int x = 0; x < 64; x += 6) {
        TEST_ASSERT_FLOAT_WITHIN(
            0.0005f, FogDensityAt(x, y, t, kWidth),
            FogDensityAt(x + static_cast<int>(kWidth), y, t, kWidth));
      }
    }
  }

  // Smooth, not per-pixel noise: neighbouring samples stay close together, so
  // the field reads as a continuous medium rather than static.
  float worst_step = 0.0f;
  for (int y = 20; y < 64; y++) {
    for (int x = 0; x < 64; x++) {
      const float here = FogDensityAt(x, y, 5000u, kWidth);
      const float right = FogDensityAt(x + 1, y, 5000u, kWidth);
      const float below = FogDensityAt(x, y + 1, 5000u, kWidth);
      const float step = std::fabs(right - here) > std::fabs(below - here)
                             ? std::fabs(right - here)
                             : std::fabs(below - here);
      if (step > worst_step)
        worst_step = step;
    }
  }
  TEST_ASSERT_TRUE(worst_step < 0.15f);

  // A degenerate repeat width is inert rather than undefined.
  TEST_ASSERT_EQUAL_FLOAT(0.0f, FogDensityAt(3.0f, 4.0f, 1234u, 0u));
}

static void test_lightning_schedule_is_irregular_bounded_and_deterministic() {
  const LightningState first = LightningAt(true, 0u);
  TEST_ASSERT_TRUE(first.intensity != 0u);
  TEST_ASSERT_EQUAL_UINT32(0u, first.event_index);

  // Cover all 16 maximum-length slots and the first event of the next cycle.
  constexpr uint64_t kScheduleSpanMs =
      static_cast<uint64_t>(kLightningMaxIntervalMs) * 16u +
      kLightningDurationMs + 1u;
  bool was_active = false;
  uint64_t previous_start_ms = 0;
  uint32_t event_count = 0;
  uint32_t active_duration_ms = 0;
  uint8_t peak_intensity = 0;
  for (uint64_t elapsed_ms = 0; elapsed_ms < kScheduleSpanMs; elapsed_ms++) {
    const LightningState state = LightningAt(true, elapsed_ms);
    const LightningState repeat = LightningAt(true, elapsed_ms);
    TEST_ASSERT_EQUAL_UINT8(state.intensity, repeat.intensity);
    TEST_ASSERT_EQUAL_UINT32(state.bolt_seed, repeat.bolt_seed);
    TEST_ASSERT_EQUAL_UINT32(state.event_index, repeat.event_index);
    TEST_ASSERT_TRUE(state.intensity <= kLightningMaxIntensity);

    const bool active = state.intensity != 0u;
    if (active && !was_active) {
      TEST_ASSERT_EQUAL_UINT32(event_count, state.event_index);
      if (event_count != 0u) {
        const uint64_t interval_ms = elapsed_ms - previous_start_ms;
        TEST_ASSERT_TRUE(interval_ms >= kLightningMinIntervalMs);
        TEST_ASSERT_TRUE(interval_ms <= kLightningMaxIntervalMs);
      }
      previous_start_ms = elapsed_ms;
      event_count++;
      active_duration_ms = 0;
      peak_intensity = 0;
    }
    if (active) {
      active_duration_ms++;
      if (state.intensity > peak_intensity)
        peak_intensity = state.intensity;
    }
    if (!active && was_active) {
      TEST_ASSERT_EQUAL_UINT32(kLightningDurationMs, active_duration_ms);
      TEST_ASSERT_EQUAL_UINT8(kLightningMaxIntensity, peak_intensity);
    }
    was_active = active;
  }
  TEST_ASSERT_TRUE(event_count >= 17u);
}

static void test_lightning_is_gated_by_storm_trait_and_survives_millis_wrap() {
  constexpr uint64_t kActiveElapsedMs = 12047u;
  for (uint8_t condition = static_cast<uint8_t>(WeatherCondition::UNKNOWN);
       condition <= static_cast<uint8_t>(WeatherCondition::HAIL_THUNDERSTORM);
       condition++) {
    const WeatherEffect effect =
        WeatherEffectFor(static_cast<WeatherCondition>(condition));
    const LightningState state = LightningAt(effect.storm, kActiveElapsedMs);
    if (effect.storm) {
      TEST_ASSERT_TRUE(state.intensity != 0u);
    } else {
      TEST_ASSERT_EQUAL_UINT8(0u, state.intensity);
      for (uint64_t elapsed_ms = 0; elapsed_ms < 30000u;
           elapsed_ms += 137u)
        TEST_ASSERT_EQUAL_UINT8(
            0u, LightningAt(effect.storm, elapsed_ms).intensity);
    }
  }

  WeatherAnimationClock clock;
  clock.Reset(0xfffff000u);
  clock.Tick(static_cast<uint32_t>(0xfffff000u + kActiveElapsedMs));
  TEST_ASSERT_EQUAL_UINT64(kActiveElapsedMs, clock.elapsed_ms());
  const LightningState direct = LightningAt(true, kActiveElapsedMs);
  const LightningState wrapped = LightningAt(true, clock.elapsed_ms());
  TEST_ASSERT_EQUAL_UINT8(direct.intensity, wrapped.intensity);
  TEST_ASSERT_EQUAL_UINT32(direct.bolt_seed, wrapped.bolt_seed);
  TEST_ASSERT_EQUAL_UINT32(direct.event_index, wrapped.event_index);
}

static void test_weather_particle_kinematics_cover_precipitation_intensity() {
  const PrecipitationKinematics drizzle = PrecipitationKinematicsFor(
      PrecipitationKind::DRIZZLE, PrecipitationIntensity::SPARSE);
  const PrecipitationKinematics rain = PrecipitationKinematicsFor(
      PrecipitationKind::RAIN, PrecipitationIntensity::MEDIUM);
  const PrecipitationKinematics heavy_rain = PrecipitationKinematicsFor(
      PrecipitationKind::RAIN, PrecipitationIntensity::HEAVY);
  const PrecipitationKinematics snow = PrecipitationKinematicsFor(
      PrecipitationKind::SNOW, PrecipitationIntensity::MEDIUM);
  const PrecipitationKinematics grains = PrecipitationKinematicsFor(
      PrecipitationKind::SNOW_GRAINS, PrecipitationIntensity::MEDIUM);
  const PrecipitationKinematics hail = PrecipitationKinematicsFor(
      PrecipitationKind::HAIL, PrecipitationIntensity::HEAVY);
  TEST_ASSERT_EQUAL_UINT32(6144u, drizzle.fall_period_ms);
  TEST_ASSERT_EQUAL_INT8(1, drizzle.drift_px);
  TEST_ASSERT_EQUAL_UINT32(3072u, rain.fall_period_ms);
  TEST_ASSERT_EQUAL_INT8(3, rain.drift_px);
  TEST_ASSERT_EQUAL_UINT32(2048u, heavy_rain.fall_period_ms);
  TEST_ASSERT_EQUAL_INT8(3, heavy_rain.drift_px);
  TEST_ASSERT_TRUE(drizzle.fall_period_ms > rain.fall_period_ms);
  TEST_ASSERT_TRUE(rain.fall_period_ms > heavy_rain.fall_period_ms);
  TEST_ASSERT_EQUAL_UINT32(11200u, snow.fall_period_ms);
  TEST_ASSERT_EQUAL_INT8(3, snow.drift_px);
  TEST_ASSERT_EQUAL_UINT32(3200u, grains.fall_period_ms);
  TEST_ASSERT_EQUAL_INT8(1, grains.drift_px);
  TEST_ASSERT_EQUAL_UINT32(1400u, hail.fall_period_ms);
  TEST_ASSERT_EQUAL_INT8(1, hail.drift_px);
  TEST_ASSERT_TRUE(snow.fall_period_ms > grains.fall_period_ms);
  TEST_ASSERT_TRUE(grains.fall_period_ms > hail.fall_period_ms);

  // Only rain resolves a cadence per intensity; the others ignore it.
  for (const PrecipitationKind kind :
       {PrecipitationKind::DRIZZLE, PrecipitationKind::SNOW,
        PrecipitationKind::SNOW_GRAINS, PrecipitationKind::HAIL}) {
    const PrecipitationKinematics sparse =
        PrecipitationKinematicsFor(kind, PrecipitationIntensity::SPARSE);
    const PrecipitationKinematics heavy =
        PrecipitationKinematicsFor(kind, PrecipitationIntensity::HEAVY);
    TEST_ASSERT_EQUAL_UINT32(sparse.fall_period_ms, heavy.fall_period_ms);
    TEST_ASSERT_EQUAL_INT8(sparse.drift_px, heavy.drift_px);
  }
  TEST_ASSERT_EQUAL_UINT32(
      600u, AnimationPhase(2000u, hail.fall_period_ms, 0u));
}

// Equalizer DSP: spectrum analysis and EqualizerProcessor windowing/levels.
static void fill_sine(float *buf, int n, float freq_bin, float amp,
                      float offset = 0.0f) {
  for (int i = 0; i < n; i++) {
    buf[i] = offset + amp * std::sin(2.0f * static_cast<float>(M_PI) *
                                      freq_bin * i / n);
  }
}

static int max_band(const SpectrumBands &s) {
  int idx = 0;
  for (int b = 1; b < kBands; b++)
    if (s.magnitude[b] > s.magnitude[idx]) idx = b;
  return idx;
}


static void test_low_tone_hits_low_band() {
  float buf[kFftSize];
  fill_sine(buf, kFftSize, 4, 1000.0f);  // 4 cycles/window = low
  const SpectrumBands s = AnalyzeWindow(buf);
  TEST_ASSERT_TRUE(s.valid);
  TEST_ASSERT_LESS_THAN(kBands / 2, max_band(s));
}

static void test_high_tone_hits_high_band() {
  float buf[kFftSize];
  // 90 cycles/window is near the top of the folded range (kBandTopBin = 96).
  fill_sine(buf, kFftSize, 90, 1000.0f);
  const SpectrumBands s = AnalyzeWindow(buf);
  TEST_ASSERT_TRUE(s.valid);
  TEST_ASSERT_GREATER_OR_EQUAL(kBands / 2, max_band(s));
}

// Distinct low vs high tones land in different bands.
static void test_low_and_high_differ() {
  float lo[kFftSize], hi[kFftSize];
  fill_sine(lo, kFftSize, 3, 1000.0f);
  fill_sine(hi, kFftSize, 180, 1000.0f);
  const SpectrumBands sl = AnalyzeWindow(lo);
  const SpectrumBands sh = AnalyzeWindow(hi);
  TEST_ASSERT_LESS_THAN(max_band(sh), max_band(sl));
}

// A pure tone of amplitude A reads ~A magnitude in its band (norm check).
static void test_magnitude_tracks_amplitude() {
  float buf[kFftSize];
  fill_sine(buf, kFftSize, 20, 1000.0f);
  const SpectrumBands s = AnalyzeWindow(buf);
  TEST_ASSERT_FLOAT_WITHIN(150.0f, 1000.0f, s.magnitude[max_band(s)]);
}

// Silence (constant DC) yields ~zero magnitude: DC removed, no AC energy.
static void test_silence_is_zero() {
  float buf[kFftSize];
  for (int i = 0; i < kFftSize; i++) buf[i] = 500.0f;  // constant offset
  const SpectrumBands s = AnalyzeWindow(buf);
  TEST_ASSERT_TRUE(s.valid);
  for (int b = 0; b < kBands; b++)
    TEST_ASSERT_FLOAT_WITHIN(1.0f, 0.0f, s.magnitude[b]);
}

// DC offset does not change the result: same tone with a large offset matches.
static void test_dc_offset_removed() {
  float a[kFftSize], b[kFftSize];
  fill_sine(a, kFftSize, 20, 1000.0f, 0.0f);
  fill_sine(b, kFftSize, 20, 1000.0f, 100000.0f);
  const SpectrumBands sa = AnalyzeWindow(a);
  const SpectrumBands sb = AnalyzeWindow(b);
  TEST_ASSERT_EQUAL(max_band(sa), max_band(sb));
  for (int i = 0; i < kBands; i++)
    TEST_ASSERT_FLOAT_WITHIN(5.0f, sa.magnitude[i], sb.magnitude[i]);
}

// Every band gets at least one bin: no band can be permanently dead. Verified by
// sweeping a tone across all bands and asserting each lights up as the max.
static void test_no_dead_bands() {
  bool lit[kBands] = {false};
  for (int bin = 1; bin < kBins; bin++) {
    float buf[kFftSize];
    fill_sine(buf, kFftSize, static_cast<float>(bin), 1000.0f);
    lit[max_band(AnalyzeWindow(buf))] = true;
  }
  for (int b = 0; b < kBands; b++) TEST_ASSERT_TRUE(lit[b]);
}

// BandForBin is monotonic non-decreasing and spans the range.
static void test_band_mapping_monotonic() {
  int prev = 0;
  for (int bin = 1; bin < kBins; bin++) {
    int b = BandForBin(bin);
    TEST_ASSERT_GREATER_OR_EQUAL(prev, b);
    TEST_ASSERT_LESS_THAN(kBands, b);
    prev = b;
  }
  TEST_ASSERT_EQUAL(kBands - 1, BandForBin(kBins - 1));
}

// --- equalizer processor ---

// Feeds a tone as 32-bit I2S words across several partial chunks; Poll yields
// one result per full window and nothing before the window is complete.
static void test_processor_windows_and_polls() {
  EqualizerProcessor proc;
  int32_t chunk[128];
  float levels[kBands];
  // First 3 chunks (384 < 512) complete no window.
  for (int c = 0; c < 3; c++) {
    for (int i = 0; i < 128; i++) {
      const int n = c * 128 + i;
      const float v = 4000.0f * std::sin(2.0f * (float) M_PI * 20 * n / kFftSize);
      chunk[i] = static_cast<int32_t>(v) << 8;  // 24-bit sample in high bits
    }
    proc.AddSamples(chunk, 128);
    TEST_ASSERT_FALSE(proc.Poll(levels));
  }
  // 4th chunk completes the 512-sample window.
  for (int i = 0; i < 128; i++) {
    const int n = 384 + i;
    const float v = 4000.0f * std::sin(2.0f * (float) M_PI * 20 * n / kFftSize);
    chunk[i] = static_cast<int32_t>(v) << 8;
  }
  proc.AddSamples(chunk, 128);
  TEST_ASSERT_TRUE(proc.Poll(levels));
  TEST_ASSERT_FALSE(proc.Poll(levels));  // consumed
  bool any = false;
  for (int b = 0; b < kBands; b++) {
    TEST_ASSERT_TRUE(levels[b] >= 0.0f && levels[b] <= 1.0f);
    if (levels[b] > 0.0f) any = true;
  }
  TEST_ASSERT_TRUE(any);
}

// Capture is continuous: several windows completed between Polls are all
// consumed and their spectra averaged, so no audio is dropped while Poll waits.
static void test_processor_averages_windows_between_polls() {
  auto fill_words = [](int32_t out[kFftSize], float bin) {
    for (int n = 0; n < kFftSize; n++) {
      const float v =
          4000.0f * std::sin(2.0f * (float) M_PI * bin * n / kFftSize);
      out[n] = static_cast<int32_t>(v) << 8;
    }
  };

  int32_t low[kFftSize], high[kFftSize];
  fill_words(low, 4.0f);
  fill_words(high, 90.0f);

  // Find which band each tone drives, using separate processors.
  EqualizerProcessor only_low, only_high;
  float lo[kBands], hi[kBands];
  only_low.AddSamples(low, kFftSize);
  TEST_ASSERT_TRUE(only_low.Poll(lo));
  only_high.AddSamples(high, kFftSize);
  TEST_ASSERT_TRUE(only_high.Poll(hi));
  int lo_band = 0, hi_band = 0;
  for (int b = 1; b < kBands; b++) {
    if (lo[b] > lo[lo_band]) lo_band = b;
    if (hi[b] > hi[hi_band]) hi_band = b;
  }
  TEST_ASSERT_NOT_EQUAL(lo_band, hi_band);

  // Feed both windows before a single Poll. No audio is dropped while Poll
  // waits, so both the low and the high tone contribute: both their bands light
  // up, and a second Poll finds nothing left to consume.
  EqualizerProcessor both;
  float mixed[kBands];
  both.AddSamples(low, kFftSize);
  both.AddSamples(high, kFftSize);
  TEST_ASSERT_TRUE(both.Poll(mixed));
  TEST_ASSERT_FALSE(both.Poll(mixed));  // both windows consumed, none dropped
  TEST_ASSERT_TRUE(mixed[lo_band] > 0.0f);
  TEST_ASSERT_TRUE(mixed[hi_band] > 0.0f);
}

// Byte input may start at any address. It decodes the same complete words as
// aligned typed input and ignores trailing incomplete bytes.
static void test_processor_accepts_misaligned_sample_bytes() {
  int32_t words[kFftSize];
  for (int n = 0; n < kFftSize; n++) {
    const float v =
        4000.0f * std::sin(2.0f * (float) M_PI * 20 * n / kFftSize);
    words[n] = static_cast<int32_t>(v * 256.0f);
  }

  EqualizerProcessor aligned;
  EqualizerProcessor misaligned;
  aligned.AddSamples(words, kFftSize);

  std::vector<uint8_t> bytes(sizeof(words) + 4);
  std::memcpy(bytes.data() + 1, words, sizeof(words));
  const size_t partial_bytes = (kFftSize - 1) * sizeof(int32_t) + 3;
  misaligned.AddSampleBytes(bytes.data() + 1, partial_bytes);

  float expected[kBands], actual[kBands];
  TEST_ASSERT_TRUE(aligned.Poll(expected));
  TEST_ASSERT_FALSE(misaligned.Poll(actual));
  misaligned.AddSampleBytes(
      bytes.data() + 1 + (kFftSize - 1) * sizeof(int32_t), sizeof(int32_t));
  TEST_ASSERT_TRUE(misaligned.Poll(actual));
  for (int b = 0; b < kBands; b++)
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, expected[b], actual[b]);
}

// The per-band floor zeroes bars below the room noise: a tone quieter than the
// calibrated floor yields all-zero levels even at the most sensitive preset.
static void test_processor_noise_gate() {
  EqualizerProcessor proc;
  int32_t s[kFftSize];
  for (int n = 0; n < kFftSize; n++) {
    const float v = 100.0f * std::sin(2.0f * (float) M_PI * 20 * n / kFftSize);
    s[n] = static_cast<int32_t>(v) << 8;  // magnitude ~100, below every floor
  }
  proc.AddSamples(s, kFftSize);
  float levels[kBands];
  TEST_ASSERT_TRUE(proc.Poll(levels));
  for (int b = 0; b < kBands; b++) TEST_ASSERT_EQUAL_FLOAT(0.0f, levels[b]);
}

// A single loud transient (a clap) does not move the auto-scale: the mapping is
// built from a percentile over a long window, so one outlier lands in the
// discarded tail and the level of a steady tone is essentially unchanged after
// the spike -- unlike a peak-follower, which the spike would send off scale.
static void test_processor_transient_does_not_move_scale() {
  EqualizerProcessor proc;
  auto feed = [&](float amp, float out[kBands]) {
    int32_t s[kFftSize];
    for (int n = 0; n < kFftSize; n++) {
      const float v = amp * std::sin(2.0f * (float) M_PI * 4 * n / kFftSize);
      s[n] = static_cast<int32_t>(v) << 8;
    }
    proc.AddSamples(s, kFftSize);
    TEST_ASSERT_TRUE(proc.Poll(out));
  };
  // Build a varied history (alternating quiet and loud) so the percentiles have
  // real spread, then record the loud tone's steady level.
  float out[kBands];
  for (int i = 0; i < 300; i++) feed(i % 2 ? 60000.0f : 9000.0f, out);
  float before[kBands];
  feed(60000.0f, before);
  // Inject one very loud transient, then measure the loud tone again.
  feed(250000.0f, out);
  float after[kBands];
  feed(60000.0f, after);
  int band = 0;
  for (int b = 1; b < kBands; b++)
    if (before[b] > before[band]) band = b;
  TEST_ASSERT_FLOAT_WITHIN(0.1f, before[band], after[band]);
}


static void test_analog_hands_point_at_the_time() {
  clock::AnalogClockModel m;
  m.Update(3, 0, 0, 0);
  clock::HandAngles a = m.Angles();
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 90.0f, a.hour);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, a.minute);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, a.second);

  // The hour hand creeps with the minutes: half past nine sits between 9 and 10.
  m.Update(21, 30, 45, 1000);
  a = m.Angles();
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 285.0f, a.hour);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 180.0f, a.minute);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 270.0f, a.second);
}

static void test_analog_hands_wrap_out_of_range_fields() {
  clock::AnalogClockModel m;
  m.Update(25, 61, 60, 0);
  const clock::HandAngles a = m.Angles();
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 30.5f, a.hour);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 6.0f, a.minute);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, a.second);
}

static void test_analog_second_hand_ticks_not_sweeps() {
  clock::AnalogClockModel m;
  m.Update(0, 0, 10, 0);
  // Past the rebound the hand rests on its step for the remainder of the second.
  m.Update(0, 0, 10, 400);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 60.0f, m.Angles().second);
  m.Update(0, 0, 10, 900);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 60.0f, m.Angles().second);
  m.Update(0, 0, 11, 1000);
  TEST_ASSERT_FLOAT_WITHIN(clock::AnalogClockModel::kReboundDeg, 66.0f,
                           m.Angles().second);
}

static void test_analog_second_hand_rebounds_onto_its_step() {
  clock::AnalogClockModel m;
  m.Update(0, 0, 1, 0);
  const float step = 6.0f;
  m.Update(0, 0, 1, clock::AnalogClockModel::kReboundMs / 6);
  const float overshoot = m.Angles().second;
  TEST_ASSERT_TRUE(overshoot > step);
  TEST_ASSERT_TRUE(overshoot <= step + clock::AnalogClockModel::kReboundDeg);
  m.Update(0, 0, 1, 2 * clock::AnalogClockModel::kReboundMs / 3);
  TEST_ASSERT_TRUE(m.Angles().second < step);
  m.Update(0, 0, 1, clock::AnalogClockModel::kReboundMs);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, step, m.Angles().second);
}

static void test_analog_second_hand_rebounds_below_zero_at_the_minute() {
  clock::AnalogClockModel m;
  m.Update(0, 0, 0, 0);
  // Short of the step at 12 o'clock is a negative angle.
  m.Update(0, 0, 0, 61);
  TEST_ASSERT_TRUE(m.Angles().second < 0.0f);
  TEST_ASSERT_TRUE(m.Angles().second > -clock::AnalogClockModel::kReboundDeg);
  m.Update(0, 0, 0, clock::AnalogClockModel::kReboundMs);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, m.Angles().second);
}

static void test_analog_wind_starts_every_hand_at_twelve() {
  clock::AnalogClockModel m;
  m.StartWind(0);
  m.Update(9, 41, 20, 0);
  const clock::HandAngles a = m.Angles();
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, a.hour);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, a.minute);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, a.second);
  TEST_ASSERT_TRUE(m.winding());
}

static void test_analog_wind_keeps_the_hands_geared() {
  clock::AnalogClockModel m;
  m.StartWind(0);
  const uint32_t hands_ms = clock::AnalogClockModel::HandsWindMs(9, 41);
  // Through the whole first phase the minute hand is exactly twelve turns of
  // the hour hand, which is the ratio the gear train would hold.
  for (uint32_t now = 0; now <= hands_ms; now += 33) {
    m.Update(9, 41, 20, now);
    const clock::HandAngles a = m.Angles();
    const float geared = std::fmod(a.hour * 12.0f, 360.0f);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, geared, a.minute);
  }
}

static void test_analog_wind_runs_every_hand_at_once() {
  clock::AnalogClockModel m;
  m.StartWind(0);
  // 09:41 is a long way round for the geared hands and a short way for the
  // second hand, so all three are travelling at the same time early on.
  const uint32_t second_ms = clock::AnalogClockModel::SecondWindMs(20);
  const uint32_t hands_ms = clock::AnalogClockModel::HandsWindMs(9, 41);
  TEST_ASSERT_TRUE(second_ms < hands_ms);
  m.Update(9, 41, 20, second_ms / 2);
  const clock::HandAngles a = m.Angles();
  TEST_ASSERT_TRUE(a.hour > 0.0f);
  TEST_ASSERT_TRUE(a.minute > 0.0f);
  TEST_ASSERT_TRUE(a.second > 0.0f);
}

static void test_analog_wind_ticks_the_second_hand_once_it_lands() {
  clock::AnalogClockModel m;
  m.StartWind(0);
  const uint32_t second_ms = clock::AnalogClockModel::SecondWindMs(20);
  const uint32_t hands_ms = clock::AnalogClockModel::HandsWindMs(9, 41);
  // It travels forward, without overshooting, onto the second it lands in.
  float previous = 0.0f;
  bool moved_partway = false;
  for (uint32_t now = 0; now < second_ms; now += 33) {
    m.Update(9, 41, 20 + static_cast<int>(now / 1000), now);
    const float angle = m.Angles().second;
    TEST_ASSERT_TRUE(angle >= previous);
    if (angle > 1.0f && angle < 125.0f)
      moved_partway = true;
    previous = angle;
  }
  TEST_ASSERT_TRUE(moved_partway);

  // Landing mid-wind, it steps with the clock while the geared hands run on.
  const int landed_second = 20 + static_cast<int>(second_ms / 1000);
  m.Update(9, 41, landed_second, second_ms);
  TEST_ASSERT_TRUE(m.winding());
  TEST_ASSERT_FLOAT_WITHIN(0.001f, landed_second * 6.0f, m.Angles().second);
  m.Update(9, 41, landed_second + 1, second_ms + 1000);
  TEST_ASSERT_FLOAT_WITHIN(clock::AnalogClockModel::kReboundDeg,
                           (landed_second + 1) * 6.0f, m.Angles().second);
  TEST_ASSERT_TRUE(m.Angles().hour < 290.5f);
  TEST_ASSERT_TRUE(m.winding());
  // The wind is over once the geared hands arrive.
  m.Update(9, 41, 24, hands_ms);
  TEST_ASSERT_FALSE(m.winding());
}

static void test_analog_wind_lands_the_second_hand_on_a_live_second() {
  // The clock runs on while the hand travels, so it is aimed at the second
  // that will be showing when it arrives, taking another turn if need be.
  const uint32_t ms = clock::AnalogClockModel::SecondWindMs(59);
  TEST_ASSERT_TRUE(ms > 1000);
  const int landed = (59 + static_cast<int>(ms / 1000)) % 60;
  clock::AnalogClockModel m;
  m.StartWind(0);
  m.Update(11, 59, 59, 0);
  m.Update(11, 59, landed, ms);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, landed * 6.0f, m.Angles().second);
}

static void test_analog_wind_runs_forward_onto_the_time() {
  clock::AnalogClockModel m;
  m.StartWind(0);
  const uint32_t hands_ms = clock::AnalogClockModel::HandsWindMs(9, 41);
  // The hour hand only ever advances, and never past its target.
  float previous = 0.0f;
  for (uint32_t now = 0; now < hands_ms; now += 33) {
    m.Update(9, 41, 20, now);
    TEST_ASSERT_TRUE(m.Angles().hour >= previous);
    TEST_ASSERT_TRUE(m.Angles().hour <= 290.5f);
    previous = m.Angles().hour;
  }
  m.Update(9, 41, 20, clock::AnalogClockModel::WindMs(9, 41, 20));
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 290.5f, m.Angles().hour);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 246.0f, m.Angles().minute);
}

static void test_analog_wind_decelerates_onto_the_time() {
  clock::AnalogClockModel m;
  m.StartWind(0);
  const uint32_t hands_ms = clock::AnalogClockModel::HandsWindMs(9, 41);
  // The last stretch of the geared phase is slower than the cruise before it.
  m.Update(9, 41, 20, hands_ms - clock::AnalogClockModel::kWindSettleMs - 100);
  const float a0 = m.Angles().hour;
  m.Update(9, 41, 20, hands_ms - clock::AnalogClockModel::kWindSettleMs - 50);
  const float cruise_step = m.Angles().hour - a0;
  m.Update(9, 41, 20, hands_ms - 50);
  const float b0 = m.Angles().hour;
  m.Update(9, 41, 20, hands_ms);
  TEST_ASSERT_TRUE(m.Angles().hour - b0 < cruise_step);
}

static void test_analog_wind_is_shorter_for_a_nearer_time() {
  // Each phase covers real distance, so a time just past 12 takes far less
  // than one just short of it.
  TEST_ASSERT_TRUE(clock::AnalogClockModel::WindMs(12, 5, 10) <
                   clock::AnalogClockModel::WindMs(11, 55, 45));
  TEST_ASSERT_EQUAL(clock::AnalogClockModel::WindMs(11, 59, 59),
                    clock::AnalogClockModel::WindMs());
  // Even the longest wind stays within a few seconds.
  TEST_ASSERT_TRUE(clock::AnalogClockModel::WindMs() < 7000);
  // Noon exactly is already home: nothing to wind.
  TEST_ASSERT_EQUAL(0, clock::AnalogClockModel::WindMs(12, 0, 0));
}

static void test_analog_wind_ignores_time_moving_on_midway() {
  clock::AnalogClockModel m;
  m.StartWind(0);
  const uint32_t wind_ms = clock::AnalogClockModel::WindMs(9, 41, 59);
  m.Update(9, 41, 59, 0);
  // The minute rolls over while the hands are still travelling.
  m.Update(9, 42, 0, wind_ms / 2);
  m.Update(9, 42, 1, wind_ms);
  TEST_ASSERT_FALSE(m.winding());
  // The wind lands on the time it was aimed at, then the movement shows the
  // real one from the next update.
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 291.0f, m.Angles().hour);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 252.0f, m.Angles().minute);
}

static void test_analog_wind_survives_millis_wraparound() {
  clock::AnalogClockModel m;
  const uint32_t before = 0xFFFFFFFFu - 100;
  m.StartWind(before);
  m.Update(9, 41, 20, before + 100);
  TEST_ASSERT_TRUE(m.winding());
  TEST_ASSERT_TRUE(m.Angles().hour > 0.0f);
  TEST_ASSERT_TRUE(m.Angles().hour < 290.5f);
  m.Update(9, 41, 20, before + clock::AnalogClockModel::WindMs(9, 41, 20));
  TEST_ASSERT_FALSE(m.winding());
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 290.5f, m.Angles().hour);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 120.0f, m.Angles().second);
}

static void test_analog_parks_every_hand_at_twelve() {
  clock::AnalogClockModel m;
  m.Update(9, 41, 20, 0);
  m.Park();
  const clock::HandAngles a = m.Angles();
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, a.hour);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, a.minute);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, a.second);
  // Parked hands hold still: no rebound runs while there is nothing to show.
  m.Park();
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, m.Angles().second);
}

static void test_analog_parked_hands_step_on_the_next_time() {
  clock::AnalogClockModel m;
  m.Park();
  // Coming out of a park at the same second the movement last showed still
  // restarts the rebound, so the hand is seen to arrive.
  m.Update(0, 0, 0, 500);
  m.Update(0, 0, 0, 500 + clock::AnalogClockModel::kReboundMs / 6);
  TEST_ASSERT_TRUE(m.Angles().second > 0.0f);
}

static void test_analog_second_hand_survives_millis_wraparound() {
  clock::AnalogClockModel m;
  const uint32_t before = 0xFFFFFFFFu - 20;
  m.Update(0, 0, 5, before);
  m.Update(0, 0, 6, before + 20);
  // 40ms after the step, still within the rebound.
  m.Update(0, 0, 6, before + 60);
  TEST_ASSERT_TRUE(m.Angles().second > 36.0f);
  m.Update(0, 0, 6, before + 20 + clock::AnalogClockModel::kReboundMs);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 36.0f, m.Angles().second);
}

static void test_binary_digits_split_the_time_per_column() {
  int d[clock::BinaryClockModel::kColumns];
  clock::BinaryClockModel::Digits(14, 5, 38, d);
  TEST_ASSERT_EQUAL(1, d[0]);
  TEST_ASSERT_EQUAL(4, d[1]);
  TEST_ASSERT_EQUAL(0, d[2]);
  TEST_ASSERT_EQUAL(5, d[3]);
  TEST_ASSERT_EQUAL(3, d[4]);
  TEST_ASSERT_EQUAL(8, d[5]);
}

static void test_binary_digits_wrap_out_of_range_fields() {
  int d[clock::BinaryClockModel::kColumns];
  clock::BinaryClockModel::Digits(25, 61, -1, d);
  TEST_ASSERT_EQUAL(0, d[0]);
  TEST_ASSERT_EQUAL(1, d[1]);
  TEST_ASSERT_EQUAL(0, d[2]);
  TEST_ASSERT_EQUAL(1, d[3]);
  TEST_ASSERT_EQUAL(5, d[4]);
  TEST_ASSERT_EQUAL(9, d[5]);
}

static void test_binary_rows_read_eight_to_one() {
  TEST_ASSERT_EQUAL(8, clock::BinaryClockModel::RowBit(0));
  TEST_ASSERT_EQUAL(4, clock::BinaryClockModel::RowBit(1));
  TEST_ASSERT_EQUAL(2, clock::BinaryClockModel::RowBit(2));
  TEST_ASSERT_EQUAL(1, clock::BinaryClockModel::RowBit(3));
  TEST_ASSERT_EQUAL(0, clock::BinaryClockModel::RowBit(4));
}

// Level of every dot once the time has been showing long enough for all
// transitions to end.
static void settle_binary(clock::BinaryClockModel &m, int hour, int minute,
                          int second, uint32_t now_ms) {
  m.Update(hour, minute, second, now_ms);
  m.Update(hour, minute, second, now_ms + clock::BinaryClockModel::LoadMs() +
                                    clock::BinaryClockModel::kGlowMs);
}

static void test_binary_settles_on_the_bits_of_each_digit() {
  clock::BinaryClockModel m;
  settle_binary(m, 14, 5, 38, 0);
  int d[clock::BinaryClockModel::kColumns];
  clock::BinaryClockModel::Digits(14, 5, 38, d);
  for (int c = 0; c < clock::BinaryClockModel::kColumns; c++) {
    for (int r = 0; r < clock::BinaryClockModel::kRows; r++) {
      const bool lit = (d[c] & clock::BinaryClockModel::RowBit(r)) != 0;
      TEST_ASSERT_FLOAT_WITHIN(0.001f, lit ? 1.0f : 0.0f, m.Level(c, r));
    }
  }
}

static void test_binary_dot_pops_past_its_rest_level_and_settles() {
  clock::BinaryClockModel m;
  settle_binary(m, 0, 0, 0, 0);
  const uint32_t base = 10000;
  // Second ones 0 -> 1 lights the 1 bit of the rightmost column.
  m.Update(0, 0, 1, base);
  const uint32_t peak_ms = static_cast<uint32_t>(
      clock::BinaryClockModel::kPopMs * clock::BinaryClockModel::kPopPeak);
  m.Update(0, 0, 1, base + peak_ms);
  const float peak = m.Level(5, 3);
  TEST_ASSERT_TRUE(peak > 1.0f);
  TEST_ASSERT_TRUE(peak <= 1.0f + clock::BinaryClockModel::kPopOvershoot);
  m.Update(0, 0, 1, base + clock::BinaryClockModel::kPopMs);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.0f, m.Level(5, 3));
}

static void test_binary_dot_fades_out_rather_than_snapping() {
  clock::BinaryClockModel m;
  settle_binary(m, 0, 0, 1, 0);
  const uint32_t base = 10000;
  m.Update(0, 0, 2, base);
  m.Update(0, 0, 2, base + clock::BinaryClockModel::kGlowMs / 2);
  const float half = m.Level(5, 3);
  TEST_ASSERT_TRUE(half > 0.0f);
  TEST_ASSERT_TRUE(half < 1.0f);
  m.Update(0, 0, 2, base + clock::BinaryClockModel::kGlowMs);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, m.Level(5, 3));
}

static void test_binary_carry_travels_right_to_left() {
  clock::BinaryClockModel m;
  settle_binary(m, 0, 0, 59, 0);
  const uint32_t base = 10000;
  // 00:00:59 -> 00:01:00 puts out the 8 bit of the seconds ones (column 5) and
  // the 4 bit of the seconds tens (column 4), and lights the 1 bit of the
  // minutes ones (column 3).
  m.Update(0, 1, 0, base);
  // The rightmost column that changed moves first; the ones left of it are
  // still waiting for their beat.
  m.Update(0, 1, 0, base + 5);
  TEST_ASSERT_TRUE(m.Level(5, 0) < 1.0f);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.0f, m.Level(4, 1));
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, m.Level(3, 3));
  m.Update(0, 1, 0, base + clock::BinaryClockModel::kRippleMs + 5);
  TEST_ASSERT_TRUE(m.Level(4, 1) < 1.0f);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, m.Level(3, 3));
  m.Update(0, 1, 0, base + 2 * clock::BinaryClockModel::kRippleMs + 5);
  TEST_ASSERT_TRUE(m.Level(3, 3) > 0.0f);
}

static void test_binary_carry_leaves_unchanged_higher_columns_alone() {
  clock::BinaryClockModel m;
  settle_binary(m, 14, 5, 38, 0);
  const uint32_t base = 10000;
  m.Update(14, 5, 39, base);
  m.Update(14, 5, 39, base + 5);
  // Only the seconds ones digit changed, so the hour columns never move.
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.0f, m.Level(0, 3));
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.0f, m.Level(1, 1));
  TEST_ASSERT_TRUE(m.Level(5, 3) > 0.0f);
}

static void test_binary_load_sweeps_the_columns_left_to_right() {
  clock::BinaryClockModel m;
  m.StartLoad(0);
  m.Update(23, 59, 59, 5);
  // Column 0 is on the sweep's first beat; the ones right of it are pending.
  TEST_ASSERT_TRUE(m.Level(0, 2) > 0.0f);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, m.Level(1, 3));
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, m.Level(5, 3));
  m.Update(23, 59, 59, clock::BinaryClockModel::kLoadStaggerMs + 5);
  TEST_ASSERT_TRUE(m.Level(1, 3) > 0.0f);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, m.Level(5, 3));
  m.Update(23, 59, 59, clock::BinaryClockModel::LoadMs());
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.0f, m.Level(5, 3));
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.0f, m.Level(0, 2));
}

static void test_binary_cleared_dots_light_on_the_next_time() {
  clock::BinaryClockModel m;
  settle_binary(m, 14, 5, 38, 0);
  m.Clear();
  for (int c = 0; c < clock::BinaryClockModel::kColumns; c++) {
    for (int r = 0; r < clock::BinaryClockModel::kRows; r++)
      TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, m.Level(c, r));
  }
  settle_binary(m, 14, 5, 38, 20000);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.0f, m.Level(1, 1));
}

static void test_binary_survives_millis_wraparound() {
  clock::BinaryClockModel m;
  const uint32_t before = 0xFFFFFFFFu - 20;
  settle_binary(m, 0, 0, 0, before - 1000);
  m.Update(0, 0, 1, before);
  m.Update(0, 0, 1, before + 20);
  TEST_ASSERT_TRUE(m.Level(5, 3) > 0.0f);
  m.Update(0, 0, 1, before + clock::BinaryClockModel::kPopMs);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.0f, m.Level(5, 3));
}

static void update_at(clock::SplitFlapModel &m, int hour, int minute,
                      uint32_t now_ms) {
  int d[clock::kFlapCount];
  clock::FlapDigits(hour, minute, d);
  m.Update(d, now_ms);
}

static void test_flap_digits_split_hour_and_minute() {
  int d[clock::kFlapCount];
  clock::FlapDigits(9, 5, d);
  TEST_ASSERT_EQUAL(0, d[0]);
  TEST_ASSERT_EQUAL(9, d[1]);
  TEST_ASSERT_EQUAL(0, d[2]);
  TEST_ASSERT_EQUAL(5, d[3]);
  clock::FlapDigits(23, 59, d);
  TEST_ASSERT_EQUAL(2, d[0]);
  TEST_ASSERT_EQUAL(3, d[1]);
  TEST_ASSERT_EQUAL(5, d[2]);
  TEST_ASSERT_EQUAL(9, d[3]);
}

static void test_flap_digits_reject_out_of_range() {
  int d[clock::kFlapCount];
  clock::FlapDigits(24, 60, d);
  for (int i = 0; i < clock::kFlapCount; i++)
    TEST_ASSERT_EQUAL(-1, d[i]);
}

static void settle(clock::SplitFlapModel &m, int hour, int minute,
                   uint32_t now_ms) {
  update_at(m, hour, minute, now_ms);
  update_at(m, hour, minute, now_ms + 30 * clock::SplitFlapModel::kStepMs);
}

static void test_split_flap_starts_from_zero() {
  clock::SplitFlapModel m;
  update_at(m, 4, 30, 0);
  // Columns start showing 0: 04:30 leaves the two zero columns still.
  TEST_ASSERT_FALSE(m.View(0).flipping);
  TEST_ASSERT_FALSE(m.View(3).flipping);
  TEST_ASSERT_TRUE(m.View(1).flipping);
}

static void test_split_flap_settles_on_target() {
  clock::SplitFlapModel m;
  settle(m, 12, 34, 0);
  const int want[clock::kFlapCount] = {1, 2, 3, 4};
  for (int i = 0; i < clock::kFlapCount; i++) {
    TEST_ASSERT_FALSE(m.View(i).flipping);
    TEST_ASSERT_EQUAL(0.0f, m.View(i).phase);
    TEST_ASSERT_EQUAL(want[i], m.View(i).to);
    TEST_ASSERT_EQUAL(want[i], m.View(i).from);
  }
}

static void test_split_flap_riffles_through_intermediate_digits() {
  clock::SplitFlapModel m;
  // Minute ones is the only column that moves, so it starts immediately.
  const uint32_t base = 0;
  update_at(m, 0, 9, 0);
  for (int step = 0; step < 9; step++) {
    // Flaps get quicker as the run goes on, so each one starts where the
    // previous ended rather than at a fixed cadence.
    const uint32_t mid = base + clock::SplitFlapModel::RunMs(step) +
                         clock::SplitFlapModel::StepMs(step) / 2;
    update_at(m, 0, 9, mid);
    const clock::FlapView v = m.View(3);
    TEST_ASSERT_TRUE(v.flipping);
    TEST_ASSERT_EQUAL(step, v.from);
    TEST_ASSERT_EQUAL(step + 1, v.to);
  }
  update_at(m, 0, 9, base + clock::SplitFlapModel::RunMs(9));
  TEST_ASSERT_FALSE(m.View(3).flipping);
  TEST_ASSERT_EQUAL(9, m.View(3).to);
}

static void test_split_flap_retargets_an_active_run() {
  clock::SplitFlapModel m;
  const uint32_t halfway = clock::SplitFlapModel::kStepMs / 2;
  update_at(m, 0, 9, 0);
  update_at(m, 0, 9, halfway);
  update_at(m, 0, 2, halfway);

  update_at(m, 0, 2, clock::SplitFlapModel::RunMs(2));
  TEST_ASSERT_FALSE(m.View(3).flipping);
  TEST_ASSERT_EQUAL(2, m.View(3).to);
}

static void test_split_flap_retarget_to_shown_finishes_a_revolution() {
  clock::SplitFlapModel m;
  const uint32_t halfway = clock::SplitFlapModel::kStepMs / 2;
  update_at(m, 0, 1, 0);
  update_at(m, 0, 1, halfway);
  update_at(m, 0, 0, halfway);

  update_at(m, 0, 0, clock::SplitFlapModel::kStepMs);
  TEST_ASSERT_TRUE(m.View(3).flipping);
  update_at(m, 0, 0, clock::SplitFlapModel::RunMs(10));
  TEST_ASSERT_FALSE(m.View(3).flipping);
  TEST_ASSERT_EQUAL(0, m.View(3).to);
}

static void test_split_flap_cancels_a_pending_run_to_the_shown_digit() {
  clock::SplitFlapModel m;
  update_at(m, 11, 11, 0);
  update_at(m, 10, 11, clock::SplitFlapModel::kStaggerMs / 2);
  update_at(m, 10, 11, clock::SplitFlapModel::kStepMs);

  TEST_ASSERT_FALSE(m.View(1).flipping);
  TEST_ASSERT_EQUAL(0, m.View(1).to);
}

static void test_split_flap_wraps_nine_to_zero() {
  clock::SplitFlapModel m;
  const uint32_t settled = 30 * clock::SplitFlapModel::kStepMs;
  settle(m, 0, 9, 0);
  // 9 -> 0 is one flap through the wrap, not nine flaps backwards.
  update_at(m, 0, 0, settled);
  update_at(m, 0, 0, settled + clock::SplitFlapModel::kStepMs / 2);
  const clock::FlapView v = m.View(3);
  TEST_ASSERT_TRUE(v.flipping);
  TEST_ASSERT_EQUAL(9, v.from);
  TEST_ASSERT_EQUAL(0, v.to);
  update_at(m, 0, 0, settled + clock::SplitFlapModel::kStepMs);
  TEST_ASSERT_FALSE(m.View(3).flipping);
  TEST_ASSERT_EQUAL(0, m.View(3).to);
}

static void test_split_flap_staggers_columns_that_start_together() {
  clock::SplitFlapModel m;
  // 11:11 starts all four columns; each waits one beat more than the last, in
  // start order, so the first one to move is not delayed.
  update_at(m, 11, 11, 0);
  TEST_ASSERT_TRUE(m.View(0).flipping);
  TEST_ASSERT_FALSE(m.View(1).flipping);
  update_at(m, 11, 11, clock::SplitFlapModel::kStaggerMs);
  TEST_ASSERT_TRUE(m.View(1).flipping);
  TEST_ASSERT_FALSE(m.View(2).flipping);
  update_at(m, 11, 11, 3 * clock::SplitFlapModel::kStaggerMs);
  TEST_ASSERT_TRUE(m.View(3).flipping);
}

static void test_split_flap_phase_advances_within_a_flap() {
  clock::SplitFlapModel m;
  update_at(m, 0, 1, 0);
  const uint32_t base = 0;
  update_at(m, 0, 1, base + clock::SplitFlapModel::kStepMs / 4);
  const float early = m.View(3).phase;
  update_at(m, 0, 1, base + 3 * clock::SplitFlapModel::kStepMs / 4);
  const float late = m.View(3).phase;
  TEST_ASSERT_TRUE(early > 0.0f && early < 1.0f);
  TEST_ASSERT_TRUE(late > early && late < 1.0f);
}

static void test_split_flap_survives_millis_wraparound() {
  clock::SplitFlapModel m;
  const uint32_t near_end = 0xFFFFFFFFu - clock::SplitFlapModel::kStepMs / 2;
  update_at(m, 0, 1, near_end);
  // The tick clock wraps mid-riffle; the column still lands on its target.
  update_at(m, 0, 1, near_end + 30 * clock::SplitFlapModel::kStepMs);
  TEST_ASSERT_FALSE(m.View(3).flipping);
  TEST_ASSERT_EQUAL(1, m.View(3).to);
}

static void test_split_flap_ignores_invalid_digits() {
  clock::SplitFlapModel m;
  const uint32_t settled = 30 * clock::SplitFlapModel::kStepMs;
  settle(m, 7, 7, 0);
  update_at(m, -1, -1, settled);
  TEST_ASSERT_EQUAL(7, m.View(1).to);
  TEST_ASSERT_EQUAL(7, m.View(3).to);
  TEST_ASSERT_FALSE(m.View(3).flipping);
}

static void test_split_flap_idles_even_while_the_clock_keeps_ticking() {
  // The minute digit changes more often than the idle interval, so a content
  // change must not postpone the idle re-seek or it would never run.
  clock::SplitFlapModel m;
  long idle_flaps = 0;
  int shown[2] = {m.View(0).to, m.View(1).to};
  for (uint32_t t = 0; t < 6 * clock::SplitFlapModel::kIdleMaxMs; t += 33) {
    const int minute = static_cast<int>(t / 60000u) % 60;
    update_at(m, 7, minute, t);
    // The hour stays 07, so any flap either hour column makes is an idle
    // re-seek rather than a content change.
    for (int i = 0; i < 2; i++) {
      if (m.View(i).to != shown[i]) {
        idle_flaps++;
        shown[i] = m.View(i).to;
      }
    }
  }
  // Six idle windows elapse, so several re-seeks of ten flaps each are due.
  // Deferring the idle on every content change would leave only a handful.
  TEST_ASSERT_TRUE(idle_flaps > 20);
}

static void test_split_flap_ramp_shortens_flaps_toward_the_cap() {
  // A run starts slow, only ever accelerates, and settles at the cap.
  TEST_ASSERT_EQUAL(clock::SplitFlapModel::kStepMs,
                    clock::SplitFlapModel::StepMs(0));
  for (int i = 1; i < clock::SplitFlapModel::kDigits; i++) {
    TEST_ASSERT_TRUE(clock::SplitFlapModel::StepMs(i) <=
                     clock::SplitFlapModel::StepMs(i - 1));
    TEST_ASSERT_TRUE(clock::SplitFlapModel::StepMs(i) >=
                     clock::SplitFlapModel::kFastStepMs);
  }
  TEST_ASSERT_EQUAL(clock::SplitFlapModel::kFastStepMs,
                    clock::SplitFlapModel::StepMs(clock::SplitFlapModel::kDigits));
}

static void test_split_flap_spin_runs_a_full_revolution_per_column() {
  clock::SplitFlapModel m;
  settle(m, 12, 34, 0);
  const uint32_t spin_at = 30 * clock::SplitFlapModel::kStepMs;
  m.SpinAll(spin_at);

  // Every column leaves its digit and comes back to it a revolution later.
  update_at(m, 12, 34, spin_at + clock::SplitFlapModel::kStepMs / 2);
  TEST_ASSERT_TRUE(m.View(0).flipping);
  TEST_ASSERT_EQUAL(1, m.View(0).from);
  TEST_ASSERT_EQUAL(2, m.View(0).to);

  const uint32_t revolution =
      clock::SplitFlapModel::RunMs(2 * clock::SplitFlapModel::kDigits);
  update_at(m, 12, 34, spin_at + 3 * clock::SplitFlapModel::kStaggerMs +
                           revolution);
  const int want[clock::kFlapCount] = {1, 2, 3, 4};
  for (int i = 0; i < clock::kFlapCount; i++) {
    TEST_ASSERT_FALSE(m.View(i).flipping);
    TEST_ASSERT_EQUAL(want[i], m.View(i).to);
  }
}

static void test_split_flap_idles_all_columns_when_quiet() {
  clock::SplitFlapModel m;
  settle(m, 12, 34, 0);

  // No idle spin before the minimum quiet interval.
  uint32_t now = clock::SplitFlapModel::kIdleMinMs - 1;
  update_at(m, 12, 34, now);
  for (int i = 0; i < clock::kFlapCount; i++)
    TEST_ASSERT_FALSE(m.View(i).flipping);

  // Once the random idle deadline arrives, the stagger starts at the left.
  bool seen = false;
  for (now = clock::SplitFlapModel::kIdleMinMs;
       now <= clock::SplitFlapModel::kIdleMaxMs && !seen;
       now += clock::SplitFlapModel::kStepMs / 2) {
    update_at(m, 12, 34, now);
    seen = m.View(0).flipping;
  }
  TEST_ASSERT_TRUE(seen);

  // After the stagger, all four columns are moving through a full revolution.
  update_at(m, 12, 34, now + 3 * clock::SplitFlapModel::kStaggerMs);
  for (int i = 0; i < clock::kFlapCount; i++)
    TEST_ASSERT_TRUE(m.View(i).flipping);

  // Every column lands back on the same displayed time.
  const uint32_t revolution =
      clock::SplitFlapModel::RunMs(2 * clock::SplitFlapModel::kDigits);
  update_at(m, 12, 34, now + revolution);
  const int want[clock::kFlapCount] = {1, 2, 3, 4};
  for (int i = 0; i < clock::kFlapCount; i++) {
    TEST_ASSERT_FALSE(m.View(i).flipping);
    TEST_ASSERT_EQUAL(want[i], m.View(i).to);
  }
}

// UTC epoch for a given calendar date at noon UTC, so a day's sun window is
// unambiguous. Days from 1970-01-01 via a plain civil-to-days algorithm.
static std::time_t UtcNoon(int y, int m, int d) {
  int yy = y - (m <= 2);
  const int era = (yy >= 0 ? yy : yy - 399) / 400;
  const unsigned yoe = static_cast<unsigned>(yy - era * 400);
  const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  const long days = static_cast<long>(era) * 146097 + static_cast<long>(doe) -
                    719468;
  return static_cast<std::time_t>(days * 86400 + 12 * 3600);
}

constexpr float kBerlinCityCenterLatitude = 52.5200f;
constexpr float kBerlinCityCenterLongitude = 13.4050f;

static pixoo::SkyState ComputeBerlinCityCenterSkyState(std::time_t epoch) {
  return pixoo::ComputeSkyState(epoch, kBerlinCityCenterLatitude,
                                kBerlinCityCenterLongitude);
}

void test_sky_invalid_inputs_are_rejected() {
  TEST_ASSERT_FALSE(ComputeBerlinCityCenterSkyState(0).valid);
  TEST_ASSERT_FALSE(pixoo::ComputeSkyState(UtcNoon(2024, 6, 21), 91.0f,
                                           kBerlinCityCenterLongitude)
                        .valid);
  TEST_ASSERT_FALSE(
      pixoo::ComputeSkyState(UtcNoon(2024, 6, 21), 50.0f, 200.0f).valid);
  TEST_ASSERT_FALSE(pixoo::ComputeSkyState(UtcNoon(2024, 6, 21), NAN,
                                           kBerlinCityCenterLongitude)
                        .valid);
}

void test_sky_moon_position_matches_jpl_reference() {
  // JPL Horizons DE441 airless apparent target-centre coordinates. The compact
  // model intentionally omits several ephemeris corrections; half a degree is
  // below one display pixel.
  const struct {
    std::time_t epoch;
    float latitude;
    float longitude;
    float azimuth;
    float altitude;
  } references[] = {
      {UtcNoon(2024, 4, 8) + 6 * 3600, 37.7749f, -122.4194f,
       127.202498855f, 47.356823275f},
      {UtcNoon(2025, 1, 13) + 8 * 3600, 51.4779f, 0.0f, 94.231961971f,
       36.774226877f},
      {UtcNoon(2026, 6, 1), -33.8688f, 151.2093f, 91.965144381f,
       53.733605875f},
      {UtcNoon(2024, 4, 8) + 12 * 3600, 37.7749f, -122.4194f,
       254.584193050f, 32.915397592f},
      {UtcNoon(2024, 4, 8) + 14 * 3600 + 55 * 60, 37.7749f, -122.4194f,
       282.198859083f, 0.203260216f},
  };
  for (const auto &reference : references) {
    const pixoo::SkyState sky = pixoo::ComputeSkyState(
        reference.epoch, reference.latitude, reference.longitude);
    TEST_ASSERT_TRUE(sky.valid);
    TEST_ASSERT_TRUE(sky.moon_above_horizon);
    TEST_ASSERT_FLOAT_WITHIN(0.5f, reference.azimuth,
                             sky.moon_azimuth_degrees);
    TEST_ASSERT_FLOAT_WITHIN(0.5f, reference.altitude,
                             sky.moon_altitude_degrees);
  }
}

void test_sky_moon_position_is_finite_and_bounded() {
  const std::time_t base = UtcNoon(2024, 4, 8) + 6 * 3600;
  const float locations[][2] = {
      {37.7749f, -122.4194f},
      {kBerlinCityCenterLatitude, kBerlinCityCenterLongitude},
      {0.0f, 180.0f},
      {90.0f, 0.0f},
      {-90.0f, 0.0f},
  };
  for (const auto &location : locations) {
    const pixoo::SkyState sky =
        pixoo::ComputeSkyState(base, location[0], location[1]);
    TEST_ASSERT_TRUE(sky.valid);
    TEST_ASSERT_TRUE(std::isfinite(sky.moon_altitude_degrees));
    TEST_ASSERT_TRUE(std::isfinite(sky.moon_azimuth_degrees));
    TEST_ASSERT_TRUE(std::isfinite(sky.moon_arc_fraction));
    TEST_ASSERT_TRUE(sky.moon_altitude_degrees >= -90.0f &&
                     sky.moon_altitude_degrees <= 90.0f);
    TEST_ASSERT_TRUE(sky.moon_azimuth_degrees >= 0.0f &&
                     sky.moon_azimuth_degrees < 360.0f);
    TEST_ASSERT_TRUE(sky.moon_arc_fraction >= 0.0f &&
                     sky.moon_arc_fraction <= 1.0f);
    TEST_ASSERT_EQUAL(sky.moon_altitude_degrees > 0.0f,
                      sky.moon_above_horizon);
  }
}

void test_sky_moon_horizon_location_and_arc_progression() {
  const std::time_t epoch = UtcNoon(2024, 4, 8) + 6 * 3600;
  const pixoo::SkyState san_francisco =
      pixoo::ComputeSkyState(epoch, 37.7749f, -122.4194f);
  const pixoo::SkyState berlin = ComputeBerlinCityCenterSkyState(epoch);
  TEST_ASSERT_TRUE(san_francisco.moon_above_horizon);
  TEST_ASSERT_TRUE(std::fabs(san_francisco.moon_azimuth_degrees -
                            berlin.moon_azimuth_degrees) > 10.0f);
  TEST_ASSERT_TRUE(std::fabs(san_francisco.moon_altitude_degrees -
                            berlin.moon_altitude_degrees) > 10.0f);

  const pixoo::SkyState below =
      pixoo::ComputeSkyState(epoch - 12 * 3600, 37.7749f, -122.4194f);
  TEST_ASSERT_FALSE(below.moon_above_horizon);
  TEST_ASSERT_TRUE(below.moon_altitude_degrees < 0.0f);

  const pixoo::SkyState rising =
      pixoo::ComputeSkyState(epoch - 3 * 3600, 37.7749f, -122.4194f);
  const pixoo::SkyState near_transit =
      pixoo::ComputeSkyState(epoch + 3 * 3600, 37.7749f, -122.4194f);
  const pixoo::SkyState setting =
      pixoo::ComputeSkyState(epoch + 6 * 3600, 37.7749f, -122.4194f);
  TEST_ASSERT_TRUE(rising.moon_above_horizon &&
                   near_transit.moon_above_horizon &&
                   setting.moon_above_horizon);
  TEST_ASSERT_TRUE(rising.moon_arc_fraction < near_transit.moon_arc_fraction);
  TEST_ASSERT_TRUE(near_transit.moon_arc_fraction <
                   setting.moon_arc_fraction);
  TEST_ASSERT_FLOAT_WITHIN(0.1f, 0.5f, near_transit.moon_arc_fraction);
}

void test_sky_moon_phase_matches_known_lunations() {
  // New moon 2024-01-11 ~11:57 UTC and full moon 2024-01-25 ~18:54 UTC.
  const pixoo::SkyState newm =
      ComputeBerlinCityCenterSkyState(UtcNoon(2024, 1, 11));
  const pixoo::SkyState full =
      ComputeBerlinCityCenterSkyState(UtcNoon(2024, 1, 25));
  TEST_ASSERT_TRUE(newm.valid && full.valid);
  TEST_ASSERT_TRUE(newm.moon_illumination < 0.05f);
  TEST_ASSERT_TRUE(full.moon_illumination > 0.95f);
  // Waxing between new and full, waning after full.
  TEST_ASSERT_TRUE(
      ComputeBerlinCityCenterSkyState(UtcNoon(2024, 1, 18)).moon_waxing);
  TEST_ASSERT_FALSE(
      ComputeBerlinCityCenterSkyState(UtcNoon(2024, 2, 1)).moon_waxing);
}

void test_sky_sun_window_matches_reference_times() {
  // Berlin solstice: sunrise ~02:4x UTC, sunset ~19:3x UTC. Noon is daytime
  // near the middle of the arc; deep night is before sunrise.
  const pixoo::SkyState noon =
      ComputeBerlinCityCenterSkyState(UtcNoon(2024, 6, 21));
  TEST_ASSERT_TRUE(noon.valid && noon.has_arc);
  TEST_ASSERT_TRUE(noon.is_daytime);
  TEST_ASSERT_TRUE(noon.day_fraction > 0.4f && noon.day_fraction < 0.6f);

  // 01:00 UTC on the same day is before sunrise -> night, fraction pinned to 0.
  const std::time_t before_sunrise = UtcNoon(2024, 6, 21) - 11 * 3600;
  const pixoo::SkyState night =
      ComputeBerlinCityCenterSkyState(before_sunrise);
  TEST_ASSERT_TRUE(night.valid);
  TEST_ASSERT_FALSE(night.is_daytime);
  TEST_ASSERT_EQUAL_FLOAT(0.0f, night.day_fraction);
}

void test_sky_solar_day_selection_follows_longitude() {
  // At 00:00 UTC the date line is near local solar noon. +180 and -180 are
  // the same meridian and must select the same sunrise/sunset window.
  const std::time_t utc_midnight = UtcNoon(2024, 6, 21) - 12 * 3600;
  const pixoo::SkyState east =
      pixoo::ComputeSkyState(utc_midnight, 0.0f, 180.0f);
  const pixoo::SkyState west =
      pixoo::ComputeSkyState(utc_midnight, 0.0f, -180.0f);
  TEST_ASSERT_TRUE(east.valid && west.valid);
  TEST_ASSERT_TRUE(east.is_daytime && west.is_daytime);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, east.day_fraction, west.day_fraction);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, east.dayness, west.dayness);

  // A western longitude whose local solar time is afternoon must not select
  // the following day's sunrise window.
  const pixoo::SkyState western_afternoon =
      pixoo::ComputeSkyState(utc_midnight, 0.0f, -150.0f);
  TEST_ASSERT_TRUE(western_afternoon.is_daytime);
  TEST_ASSERT_TRUE(western_afternoon.day_fraction > 0.5f);
  TEST_ASSERT_EQUAL_FLOAT(1.0f, western_afternoon.dayness);
}

void test_sky_day_fraction_rises_from_sunrise_to_sunset() {
  const std::time_t base = UtcNoon(2024, 6, 21);
  const pixoo::SkyState morning =
      ComputeBerlinCityCenterSkyState(base - 8 * 3600);  // ~04:00 UTC
  const pixoo::SkyState afternoon =
      ComputeBerlinCityCenterSkyState(base + 6 * 3600);  // ~18:00 UTC
  TEST_ASSERT_TRUE(morning.is_daytime && afternoon.is_daytime);
  TEST_ASSERT_TRUE(morning.day_fraction < 0.5f);
  TEST_ASSERT_TRUE(afternoon.day_fraction > 0.5f);
  TEST_ASSERT_TRUE(afternoon.day_fraction > morning.day_fraction);
}

void test_sky_dayness_ramps_across_twilight() {
  const std::time_t base = UtcNoon(2024, 6, 21);
  // Deep day and deep night sit at the extremes.
  TEST_ASSERT_EQUAL_FLOAT(1.0f, ComputeBerlinCityCenterSkyState(base).dayness);
  const pixoo::SkyState midnight =
      ComputeBerlinCityCenterSkyState(base - 12 * 3600);
  TEST_ASSERT_TRUE(midnight.dayness < 0.01f);
  // Sunrise this Berlin day is ~02:43 UTC; at that moment dayness sits near
  // the middle of its ramp rather than pinned to 0 or 1.
  const std::time_t sunrise = base - 9 * 3600 - 17 * 60;
  const pixoo::SkyState dawn = ComputeBerlinCityCenterSkyState(sunrise);
  TEST_ASSERT_TRUE(dawn.dayness > 0.2f && dawn.dayness < 0.8f);
}

void test_sky_polar_day_and_night_have_no_arc() {
  // Far north at solstice: sun never sets (polar day).
  const pixoo::SkyState polar_day =
      pixoo::ComputeSkyState(UtcNoon(2024, 6, 21), 78.0f, 15.0f);
  TEST_ASSERT_TRUE(polar_day.valid);
  TEST_ASSERT_FALSE(polar_day.has_arc);
  TEST_ASSERT_TRUE(polar_day.is_daytime);
  // Far north at winter solstice: sun never rises (polar night).
  const pixoo::SkyState polar_night =
      pixoo::ComputeSkyState(UtcNoon(2024, 12, 21), 78.0f, 15.0f);
  TEST_ASSERT_TRUE(polar_night.valid);
  TEST_ASSERT_FALSE(polar_night.has_arc);
  TEST_ASSERT_FALSE(polar_night.is_daytime);
}

struct LifeCell {
  int x;
  int y;
};

static void SetLifeCells(GameOfLifeModel &model,
                         const std::vector<LifeCell> &cells) {
  for (const LifeCell &cell : cells)
    model.SetAlive(cell.x, cell.y, true);
}

static void AssertLifeBoard(const GameOfLifeModel &model,
                            const std::vector<LifeCell> &alive_cells) {
  for (int y = 0; y < GameOfLifeModel::kHeight; y++) {
    for (int x = 0; x < GameOfLifeModel::kWidth; x++) {
      bool expected = false;
      for (const LifeCell &cell : alive_cells) {
        if (cell.x == x && cell.y == y) {
          expected = true;
          break;
        }
      }
      TEST_ASSERT_EQUAL_UINT8(expected ? 1 : 0, model.Alive(x, y) ? 1 : 0);
    }
  }
}

static void test_life_default_board_is_blank() {
  GameOfLifeModel model;
  TEST_ASSERT_EQUAL(64, GameOfLifeModel::kWidth);
  TEST_ASSERT_EQUAL(64, GameOfLifeModel::kHeight);
  TEST_ASSERT_EQUAL(4096, GameOfLifeModel::kCellCount);
  AssertLifeBoard(model, {});
}

static void test_life_set_alive_sets_and_clears_a_cell() {
  GameOfLifeModel model;
  model.SetAlive(17, 23, true);
  TEST_ASSERT_TRUE(model.Alive(17, 23));
  model.SetAlive(17, 23, false);
  TEST_ASSERT_FALSE(model.Alive(17, 23));
}

static void test_life_clear_empties_the_board() {
  GameOfLifeModel model;
  SetLifeCells(model, {{0, 0}, {32, 32}, {63, 63}});
  model.Clear();
  AssertLifeBoard(model, {});
}

static void test_life_bounded_access_ignores_outside_coordinates() {
  GameOfLifeModel model;
  model.SetAlive(-1, 0, true);
  model.SetAlive(0, -1, true);
  model.SetAlive(GameOfLifeModel::kWidth, 0, true);
  model.SetAlive(0, GameOfLifeModel::kHeight, true);
  TEST_ASSERT_FALSE(model.Alive(-1, 0));
  TEST_ASSERT_FALSE(model.Alive(0, -1));
  TEST_ASSERT_FALSE(model.Alive(GameOfLifeModel::kWidth, 0));
  TEST_ASSERT_FALSE(model.Alive(0, GameOfLifeModel::kHeight));
  AssertLifeBoard(model, {});
}

static void test_life_underpopulation_kills_a_live_cell() {
  GameOfLifeModel model;
  model.SetAlive(32, 32, true);
  TEST_ASSERT_TRUE(model.Step());
  TEST_ASSERT_FALSE(model.Alive(32, 32));
}

static void test_life_overpopulation_kills_a_live_cell() {
  GameOfLifeModel model;
  SetLifeCells(model, {{32, 32}, {31, 32}, {33, 32}, {32, 31}, {32, 33}});
  model.Step();
  TEST_ASSERT_FALSE(model.Alive(32, 32));
}

static void test_life_live_cell_survives_with_two_neighbors() {
  GameOfLifeModel model;
  SetLifeCells(model, {{32, 32}, {31, 32}, {33, 32}});
  model.Step();
  TEST_ASSERT_TRUE(model.Alive(32, 32));
}

static void test_life_live_cell_survives_with_three_neighbors() {
  GameOfLifeModel model;
  SetLifeCells(model, {{32, 32}, {31, 32}, {33, 32}, {32, 31}});
  model.Step();
  TEST_ASSERT_TRUE(model.Alive(32, 32));
}

static void test_life_dead_cell_is_born_with_exactly_three_neighbors() {
  GameOfLifeModel model;
  SetLifeCells(model, {{31, 31}, {32, 31}, {33, 31}});
  model.Step();
  TEST_ASSERT_TRUE(model.Alive(32, 32));
}

static void test_life_dead_cell_is_not_born_with_two_neighbors() {
  GameOfLifeModel model;
  SetLifeCells(model, {{31, 32}, {33, 32}});
  model.Step();
  TEST_ASSERT_FALSE(model.Alive(32, 32));
  TEST_ASSERT_FALSE(model.Alive(31, 32));
  TEST_ASSERT_FALSE(model.Alive(33, 32));
}

static void test_life_centered_block_is_a_still_life() {
  GameOfLifeModel model;
  const std::vector<LifeCell> block = {
      {31, 31}, {32, 31}, {31, 32}, {32, 32}};
  SetLifeCells(model, block);
  TEST_ASSERT_FALSE(model.Step());
  AssertLifeBoard(model, block);
}

static void test_life_centered_blinker_has_period_two() {
  GameOfLifeModel model;
  const std::vector<LifeCell> horizontal = {{31, 32}, {32, 32}, {33, 32}};
  const std::vector<LifeCell> vertical = {{32, 31}, {32, 32}, {32, 33}};
  SetLifeCells(model, horizontal);
  TEST_ASSERT_TRUE(model.Step());
  AssertLifeBoard(model, vertical);
  TEST_ASSERT_TRUE(model.Step());
  AssertLifeBoard(model, horizontal);
}

static void test_life_canonical_glider_moves_one_cell_diagonally_after_four_steps() {
  GameOfLifeModel model;
  const std::vector<LifeCell> initial = {
      {31, 30}, {32, 31}, {30, 32}, {31, 32}, {32, 32}};
  const std::vector<LifeCell> after_four_steps = {
      {32, 31}, {33, 32}, {31, 33}, {32, 33}, {33, 33}};
  SetLifeCells(model, initial);
  for (int i = 0; i < 4; i++)
    model.Step();
  AssertLifeBoard(model, after_four_steps);
}

static void test_life_edges_do_not_wrap() {
  GameOfLifeModel model;
  SetLifeCells(model, {{0, 0}, {0, 63}, {63, 0}});
  model.Step();
  AssertLifeBoard(model, {});
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_analog_hands_point_at_the_time);
  RUN_TEST(test_analog_hands_wrap_out_of_range_fields);
  RUN_TEST(test_analog_second_hand_ticks_not_sweeps);
  RUN_TEST(test_analog_second_hand_rebounds_onto_its_step);
  RUN_TEST(test_analog_second_hand_rebounds_below_zero_at_the_minute);
  RUN_TEST(test_analog_wind_starts_every_hand_at_twelve);
  RUN_TEST(test_analog_wind_keeps_the_hands_geared);
  RUN_TEST(test_analog_wind_runs_every_hand_at_once);
  RUN_TEST(test_analog_wind_ticks_the_second_hand_once_it_lands);
  RUN_TEST(test_analog_wind_lands_the_second_hand_on_a_live_second);
  RUN_TEST(test_analog_wind_runs_forward_onto_the_time);
  RUN_TEST(test_analog_wind_decelerates_onto_the_time);
  RUN_TEST(test_analog_wind_is_shorter_for_a_nearer_time);
  RUN_TEST(test_analog_wind_ignores_time_moving_on_midway);
  RUN_TEST(test_analog_wind_survives_millis_wraparound);
  RUN_TEST(test_analog_parks_every_hand_at_twelve);
  RUN_TEST(test_analog_parked_hands_step_on_the_next_time);
  RUN_TEST(test_analog_second_hand_survives_millis_wraparound);
  RUN_TEST(test_binary_digits_split_the_time_per_column);
  RUN_TEST(test_binary_digits_wrap_out_of_range_fields);
  RUN_TEST(test_binary_rows_read_eight_to_one);
  RUN_TEST(test_binary_settles_on_the_bits_of_each_digit);
  RUN_TEST(test_binary_dot_pops_past_its_rest_level_and_settles);
  RUN_TEST(test_binary_dot_fades_out_rather_than_snapping);
  RUN_TEST(test_binary_carry_travels_right_to_left);
  RUN_TEST(test_binary_carry_leaves_unchanged_higher_columns_alone);
  RUN_TEST(test_binary_load_sweeps_the_columns_left_to_right);
  RUN_TEST(test_binary_cleared_dots_light_on_the_next_time);
  RUN_TEST(test_binary_survives_millis_wraparound);
  RUN_TEST(test_flap_digits_split_hour_and_minute);
  RUN_TEST(test_flap_digits_reject_out_of_range);
  RUN_TEST(test_split_flap_starts_from_zero);
  RUN_TEST(test_split_flap_settles_on_target);
  RUN_TEST(test_split_flap_riffles_through_intermediate_digits);
  RUN_TEST(test_split_flap_retargets_an_active_run);
  RUN_TEST(test_split_flap_retarget_to_shown_finishes_a_revolution);
  RUN_TEST(test_split_flap_cancels_a_pending_run_to_the_shown_digit);
  RUN_TEST(test_split_flap_wraps_nine_to_zero);
  RUN_TEST(test_split_flap_staggers_columns_that_start_together);
  RUN_TEST(test_split_flap_phase_advances_within_a_flap);
  RUN_TEST(test_split_flap_survives_millis_wraparound);
  RUN_TEST(test_split_flap_ignores_invalid_digits);
  RUN_TEST(test_split_flap_idles_even_while_the_clock_keeps_ticking);
  RUN_TEST(test_split_flap_ramp_shortens_flaps_toward_the_cap);
  RUN_TEST(test_split_flap_spin_runs_a_full_revolution_per_column);
  RUN_TEST(test_split_flap_idles_all_columns_when_quiet);
  RUN_TEST(test_low_tone_hits_low_band);
  RUN_TEST(test_high_tone_hits_high_band);
  RUN_TEST(test_low_and_high_differ);
  RUN_TEST(test_magnitude_tracks_amplitude);
  RUN_TEST(test_silence_is_zero);
  RUN_TEST(test_dc_offset_removed);
  RUN_TEST(test_no_dead_bands);
  RUN_TEST(test_band_mapping_monotonic);
  RUN_TEST(test_processor_windows_and_polls);
  RUN_TEST(test_processor_averages_windows_between_polls);
  RUN_TEST(test_processor_accepts_misaligned_sample_bytes);
  RUN_TEST(test_processor_noise_gate);
  RUN_TEST(test_processor_transient_does_not_move_scale);
  RUN_TEST(test_open_meteo_url_includes_complete_forecast_query);
  RUN_TEST(test_open_meteo_url_handles_widest_coordinates);
  RUN_TEST(test_refresh_policy_requests_initially);
  RUN_TEST(test_refresh_policy_keeps_success_fresh_until_stale);
  RUN_TEST(test_refresh_policy_refreshes_when_success_becomes_stale);
  RUN_TEST(test_refresh_policy_preserves_same_location_data_after_failure);
  RUN_TEST(test_refresh_policy_retries_failure_after_backoff);
  RUN_TEST(test_refresh_policy_forced_invalidation_hides_old_data);
  RUN_TEST(test_refresh_policy_rejects_old_inflight_completion);
  RUN_TEST(test_refresh_policy_handles_millis_wraparound);
  RUN_TEST(test_wmo_clear_maps_without_time_of_day);
  RUN_TEST(test_wmo_maps_each_group);
  RUN_TEST(test_wmo_unknown_code_falls_back);
  RUN_TEST(test_weather_effect_maps_every_condition);
  RUN_TEST(test_weather_transition_first_condition_is_settled);
  RUN_TEST(test_weather_transition_blends_and_settles);
  RUN_TEST(test_weather_transition_defers_retarget_without_jump);
  RUN_TEST(test_weather_transition_uses_latest_deferred_condition);
  RUN_TEST(test_weather_transition_duration_scales_with_changed_traits);
  RUN_TEST(test_weather_mix_moves_graded_traits_continuously);
  RUN_TEST(test_weather_mix_hands_over_precipitation_by_field);
  RUN_TEST(test_weather_animation_clock_resume_rebases_without_catch_up);
  RUN_TEST(test_weather_animation_clock_survives_millis_wraparound);
  RUN_TEST(test_weather_animation_phase_and_hash_are_deterministic);
  RUN_TEST(test_fog_density_is_bounded_smooth_and_horizontally_seamless);
  RUN_TEST(test_lightning_schedule_is_irregular_bounded_and_deterministic);
  RUN_TEST(test_lightning_is_gated_by_storm_trait_and_survives_millis_wrap);
  RUN_TEST(test_weather_particle_kinematics_cover_precipitation_intensity);
  RUN_TEST(test_display_degrees_rounds_to_nearest_without_signed_zero);
  RUN_TEST(test_select_now_matches_current_hour);
  RUN_TEST(test_select_now_flags_the_fetch_hour);
  RUN_TEST(test_select_now_wraps_past_midnight);
  RUN_TEST(test_select_now_falls_back_without_clock);
  RUN_TEST(test_select_now_hour_absent_from_series);
  RUN_TEST(test_select_now_truncates_at_series_end);
  RUN_TEST(test_sky_invalid_inputs_are_rejected);
  RUN_TEST(test_sky_moon_position_matches_jpl_reference);
  RUN_TEST(test_sky_moon_position_is_finite_and_bounded);
  RUN_TEST(test_sky_moon_horizon_location_and_arc_progression);
  RUN_TEST(test_sky_moon_phase_matches_known_lunations);
  RUN_TEST(test_sky_sun_window_matches_reference_times);
  RUN_TEST(test_sky_solar_day_selection_follows_longitude);
  RUN_TEST(test_sky_day_fraction_rises_from_sunrise_to_sunset);
  RUN_TEST(test_sky_dayness_ramps_across_twilight);
  RUN_TEST(test_sky_polar_day_and_night_have_no_arc);
  RUN_TEST(test_life_default_board_is_blank);
  RUN_TEST(test_life_set_alive_sets_and_clears_a_cell);
  RUN_TEST(test_life_clear_empties_the_board);
  RUN_TEST(test_life_bounded_access_ignores_outside_coordinates);
  RUN_TEST(test_life_underpopulation_kills_a_live_cell);
  RUN_TEST(test_life_overpopulation_kills_a_live_cell);
  RUN_TEST(test_life_live_cell_survives_with_two_neighbors);
  RUN_TEST(test_life_live_cell_survives_with_three_neighbors);
  RUN_TEST(test_life_dead_cell_is_born_with_exactly_three_neighbors);
  RUN_TEST(test_life_dead_cell_is_not_born_with_two_neighbors);
  RUN_TEST(test_life_centered_block_is_a_still_life);
  RUN_TEST(test_life_centered_blinker_has_period_two);
  RUN_TEST(test_life_canonical_glider_moves_one_cell_diagonally_after_four_steps);
  RUN_TEST(test_life_edges_do_not_wrap);
  return UNITY_END();
}
