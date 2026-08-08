#include "static_weather_source.h"

namespace esphome::pixoo64_render_test {

uint32_t StaticWeatherSource::current_render_time_ms_{0};

void StaticWeatherSource::SetCurrentRenderTime(uint32_t now_ms) {
  current_render_time_ms_ = now_ms;
}

pixoo::WeatherCondition StaticWeatherSource::active_condition_() const {
  if (this->has_condition_after_ &&
      deadline_reached_(current_render_time_ms_, this->condition_after_ms_))
    return this->condition_after_;
  return this->condition_;
}

pixoo::WeatherData StaticWeatherSource::Data() const {
  pixoo::WeatherData d{};
  d.valid = true;
  d.has_location = this->has_location_;
  d.latitude = this->latitude_;
  d.longitude = this->longitude_;
  const pixoo::WeatherCondition condition = this->active_condition_();
  d.condition = condition;
  d.is_night = this->is_night_;
  d.has_temperature = true;
  d.temperature = this->temperature_;
  d.has_feels_like = true;
  d.feels_like = this->apparent_;
  d.has_humidity = true;
  d.humidity = this->humidity_;
  d.has_high = true;
  d.high = this->high_;
  d.has_low = true;
  d.low = this->low_;

  // Entry 0 is the current hour; the rest are the forecast columns.
  const int count =
      1 + static_cast<int>(this->forecast_temperatures_.size());
  d.hour_count = count < pixoo::kMaxHourSamples ? count
                                                : pixoo::kMaxHourSamples;
  for (int i = 0; i < d.hour_count; i++) {
    pixoo::WeatherHourData &h = d.hours[i];
    h.valid = true;
    h.hour_of_day = (this->start_hour_ + i) % 24;
    h.condition = condition;
    h.is_night = this->is_night_;
    h.temperature = i == 0 ? this->temperature_
                           : this->forecast_temperatures_[i - 1];
    h.feels_like = this->apparent_;
    h.humidity = this->humidity_;
  }
  return d;
}

}  // namespace esphome::pixoo64_render_test
