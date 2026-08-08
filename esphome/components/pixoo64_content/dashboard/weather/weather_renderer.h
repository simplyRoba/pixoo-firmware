#pragma once

#include <string>

#include "esphome/components/display/display.h"
#include "esphome/components/font/font.h"
#include "sky_astronomy.h"
#include "weather_icon.h"

namespace esphome::pixoo64::weather {

using pixoo::kForecastHours;

// One column of the hourly forecast strip.
struct WeatherHour {
  bool valid{false};
  std::string label;  // e.g. "15" or "3p"
  WeatherCondition condition{WeatherCondition::UNKNOWN};
  bool is_night{false};
  float temperature{0.0f};
};

// Everything the weather dashboard draws, as plain data. Data gathering lives in
// the WeatherModel; this struct is what the renderer consumes, so it can be fed
// sample data by the host render test.
struct WeatherViewModel {
  WeatherCondition condition{WeatherCondition::UNKNOWN};
  bool is_night{false};

  bool has_temperature{false};
  float temperature{0.0f};

  bool has_feels_like{false};
  float feels_like{0.0f};

  bool has_high{false};
  float high{0.0f};
  bool has_low{false};
  float low{0.0f};

  bool has_humidity{false};
  float humidity{0.0f};

  std::string time_text;  // "14:05"; empty hides the clock
  std::string date_text;  // "Mon 21"; empty hides the date

  WeatherHour hours[kForecastHours];

  // Derived sun/moon state for the sun-arc presentation. Invalid when there is
  // no clock or no location; a face that needs it checks sky.valid.
  pixoo::SkyState sky;
};

// Fonts the layout uses, by role.
struct WeatherFonts {
  font::Font *small{nullptr};  // 3x5 tiny (labels, stats, forecast)
  font::Font *big{nullptr};    // 16px (hero temperature)
};

// Render `view` onto `display` using `fonts`. Pure function: no sensor access.
// `anim` places the hero icon in its loop; the default is every loop at rest.
void RenderWeather(display::Display &display, const WeatherViewModel &view,
                   const WeatherFonts &fonts, const IconAnimation &anim = {});

// Draw a placeholder shown while the source has no data yet.
void RenderWeatherLoading(display::Display &display, const WeatherFonts &fonts);

}  // namespace esphome::pixoo64::weather
