#pragma once

namespace pixoo {

// Weather condition set, covering every state the Open-Meteo WMO weather codes
// produce. UNKNOWN is the fallback for an unrecognised input. Day and night are
// not conditions: they travel alongside as an is_night flag, so any condition
// has both variants.
enum class WeatherCondition {
  UNKNOWN,
  SUNNY,
  PARTLYCLOUDY,
  CLOUDY,
  FOG,
  DRIZZLE,
  FREEZING_DRIZZLE,
  RAINY,
  POURING,
  FREEZING_RAIN,
  SNOWY,
  SNOW_GRAINS,
  THUNDERSTORM,
  HAIL_THUNDERSTORM,
};

// Three columns: at 64px wide this gives ~21px per column, enough for a
// divider line plus a two-digit negative temperature with its degree mark.
constexpr int kForecastHours = 3;

// A temperature as whole degrees for display. Halves round away from zero, so
// a reading is never pulled toward an adjacent value by the parity of its
// neighbour, and a value between -0.5 and 0 resolves to 0 rather than a signed
// zero that would print as "-0".
int DisplayDegrees(float celsius);

// Raw hourly samples buffered from a fetch. Wide enough that the wall clock,
// advancing between fetches (up to the max refresh interval), always still has
// the current hour plus kForecastHours ahead available to select.
constexpr int kMaxHourSamples = 12;

// One hourly sample (used both for the raw series and the selected columns).
struct WeatherHourData {
  bool valid{false};
  WeatherCondition condition{WeatherCondition::UNKNOWN};
  bool is_night{false};
  float temperature{0.0f};
  float feels_like{0.0f};
  float humidity{0.0f};
  int hour_of_day{0};  // 0..23, local time of this forecast hour
};

// Plain weather data, independent of any data source (Open-Meteo fetch, Home
// Assistant, or a test). Optional fields carry a presence flag; a false flag
// means the value is unknown and should not be shown.
struct WeatherData {
  bool valid{false};  // set once a source has populated current conditions
  // Location the snapshot was fetched for; has_location gates any use of it
  // (e.g. deriving sun/moon state). The source owns location, so it travels
  // with the data rather than being read separately downstream.
  bool has_location{false};
  float latitude{0.0f};
  float longitude{0.0f};
  WeatherCondition condition{WeatherCondition::UNKNOWN};
  bool is_night{false};
  bool has_temperature{false};
  float temperature{0.0f};
  bool has_feels_like{false};
  float feels_like{0.0f};
  bool has_humidity{false};
  float humidity{0.0f};
  bool has_high{false};
  float high{0.0f};
  bool has_low{false};
  float low{0.0f};
  // Raw hourly series starting at (or near) the fetch hour; hour_count entries.
  WeatherHourData hours[kMaxHourSamples];
  int hour_count{0};
};

// Wall-clock selection over the raw hourly series. has_current_hour is set when
// the series contains an entry for current_hour_of_day; `current` is then that
// entry (used for the hero readings so they roll on the hour between fetches),
// and `columns` are the kForecastHours entries after it. When no entry matches
// (no clock, or the series does not cover the hour), has_current_hour is false
// and `columns` fall back to the first entries after the series start.
//
// The series begins at the hour the snapshot was fetched in, so a selection
// landing on that first entry means the clock has not left that hour yet.
// current_is_fetch_hour reports it: an hourly entry is the value forecast for
// the top of its hour, while the snapshot's own current readings are the live
// observation for the same hour, so the live values stay authoritative until
// the clock moves on.
struct WeatherNow {
  bool has_current_hour{false};
  bool current_is_fetch_hour{false};
  WeatherHourData current;
  WeatherHourData columns[kForecastHours];
};

WeatherNow SelectWeatherNow(const WeatherData &data, int current_hour_of_day);

}  // namespace pixoo
