#include "open_meteo_url.h"

#include <cstdio>

#include "weather_data.h"

namespace pixoo {

std::string BuildOpenMeteoForecastUrl(float latitude, float longitude) {
  // Coordinates are bounded by the persisted entity ranges (-90..90 and
  // -180..180), so this buffer comfortably holds their fixed-point form.
  char coordinates[48];
  const int written = std::snprintf(coordinates, sizeof(coordinates),
                                    "%.4f&longitude=%.4f", latitude, longitude);

  std::string url;
  url.reserve(280);
  url.append("https://api.open-meteo.com/v1/forecast?latitude=");
  if (written > 0 && static_cast<size_t>(written) < sizeof(coordinates))
    url.append(coordinates, static_cast<size_t>(written));
  url.append("&current=temperature_2m,relative_humidity_2m,apparent_temperature,"
             "weather_code,is_day");
  url.append("&hourly=temperature_2m,weather_code,apparent_temperature,"
             "relative_humidity_2m,is_day");
  url.append("&daily=temperature_2m_max,temperature_2m_min");
  url.append("&forecast_days=2&forecast_hours=");
  url.append(std::to_string(kMaxHourSamples));
  url.append("&timezone=auto");
  return url;
}

}  // namespace pixoo
