#pragma once

#include <cstdint>
#include <vector>

#include "weather_data.h"
#include "weather_source.h"

namespace esphome::pixoo64_render_test {

// Test adapter for the WeatherSource port: serves a fixed snapshot with no
// network or wall clock, taking OpenMeteoSource's place so the render test
// drives the production model path.
//
// The snapshot is an hourly series starting at start_hour; that entry supplies
// the current conditions and the rest are the forecast columns.
class StaticWeatherSource : public pixoo::WeatherSource {
 public:
  void set_condition(pixoo::WeatherCondition c) { this->condition_ = c; }
  void set_available_at(uint32_t now_ms) {
    this->available_at_ms_ = now_ms;
    this->has_available_at_ = true;
  }
  void set_condition_after(pixoo::WeatherCondition c, uint32_t now_ms) {
    this->condition_after_ = c;
    this->condition_after_ms_ = now_ms;
    this->has_condition_after_ = true;
  }
  void set_night(bool is_night) { this->is_night_ = is_night; }
  void set_current(float temperature, float apparent, float humidity) {
    this->temperature_ = temperature;
    this->apparent_ = apparent;
    this->humidity_ = humidity;
  }
  void set_daily(float high, float low) {
    this->high_ = high;
    this->low_ = low;
  }
  void set_start_hour(int hour) { this->start_hour_ = hour; }
  void set_location(float latitude, float longitude) {
    this->latitude_ = latitude;
    this->longitude_ = longitude;
    this->has_location_ = true;
  }
  void add_forecast_temperature(float t) {
    this->forecast_temperatures_.push_back(t);
  }

  // RenderTestDisplay sets this test-only clock before every render.
  static void SetCurrentRenderTime(uint32_t now_ms);

  void RequestRefresh() override {}
  bool HasData() const override {
    return !this->has_available_at_ ||
           deadline_reached_(current_render_time_ms_, this->available_at_ms_);
  }
  pixoo::WeatherData Data() const override;

 protected:
  // Deadlines must be less than half the uint32_t range ahead of now.
  static bool deadline_reached_(uint32_t now_ms, uint32_t deadline_ms) {
    return static_cast<int32_t>(now_ms - deadline_ms) >= 0;
  }

  pixoo::WeatherCondition active_condition_() const;

  static uint32_t current_render_time_ms_;
  pixoo::WeatherCondition condition_{pixoo::WeatherCondition::UNKNOWN};
  uint32_t available_at_ms_{0};
  bool has_available_at_{false};
  pixoo::WeatherCondition condition_after_{pixoo::WeatherCondition::UNKNOWN};
  uint32_t condition_after_ms_{0};
  bool has_condition_after_{false};
  bool is_night_{false};
  float temperature_{0.0f};
  float apparent_{0.0f};
  float humidity_{0.0f};
  float high_{0.0f};
  float low_{0.0f};
  int start_hour_{0};
  std::vector<float> forecast_temperatures_;
  float latitude_{0.0f};
  float longitude_{0.0f};
  bool has_location_{false};
};

}  // namespace esphome::pixoo64_render_test
