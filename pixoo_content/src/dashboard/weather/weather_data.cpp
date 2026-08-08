#include "weather_data.h"

#include <cmath>

namespace pixoo {

int DisplayDegrees(float celsius) {
  if (!std::isfinite(celsius))
    return 0;
  const int degrees = static_cast<int>(std::round(celsius));
  return degrees == 0 ? 0 : degrees;
}

WeatherNow SelectWeatherNow(const WeatherData &data, int current_hour_of_day) {
  WeatherNow now;

  int start = -1;
  if (current_hour_of_day >= 0) {
    for (int i = 0; i < data.hour_count && i < kMaxHourSamples; i++) {
      if (data.hours[i].valid &&
          data.hours[i].hour_of_day == current_hour_of_day) {
        start = i;
        break;
      }
    }
  }

  if (start >= 0) {
    now.has_current_hour = true;
    now.current_is_fetch_hour = start == 0;
    now.current = data.hours[start];
  }

  // Columns are the hours after the current one; fall back to the series start
  // when the current hour is unknown.
  const int first = start >= 0 ? start + 1 : 0;
  for (int c = 0; c < kForecastHours; c++) {
    const int src = first + c;
    if (src < data.hour_count && src < kMaxHourSamples && data.hours[src].valid)
      now.columns[c] = data.hours[src];
  }

  return now;
}

}  // namespace pixoo
