#pragma once

#include <cstdio>
#include <string>

#include "esphome/components/time/real_time_clock.h"
#include "sky_astronomy.h"
#include "weather_renderer.h"
#include "weather_source.h"
#include "wmo.h"

namespace esphome::pixoo64::weather {

using pixoo::kForecastHours;
using pixoo::WeatherSource;

// Gathers weather values from the WeatherSource port plus the clock and
// produces a WeatherViewModel for the renderer. It signals demand via
// RequestRefresh() whenever weather is being shown; which provider answers is
// the adapter's concern. Optional fields carry a presence flag; a missing one
// is not shown.
class WeatherModel {
 public:
  void set_source(WeatherSource *source) {
    this->source_ = source;
    this->sky_cache_valid_ = false;
  }

  // Signal demand for fresh data (called while the weather dashboard is shown).
  void RequestRefresh() {
    if (this->source_ != nullptr)
      this->source_->RequestRefresh();
  }

  // True until the source has delivered its first data — the moment to show a
  // loading animation instead of an empty dashboard.
  bool loading() const {
    return this->source_ != nullptr && !this->source_->HasData();
  }
  void set_time(time::RealTimeClock *t) {
    this->time_ = t;
    this->sky_cache_valid_ = false;
  }
  // Pin the clock to a fixed UTC epoch; when set it takes precedence over the
  // RTC, making the clock, date, and forecast labels deterministic.
  void set_fixed_time(time_t epoch) {
    this->fixed_epoch_ = epoch;
    this->has_fixed_time_ = true;
    this->sky_cache_valid_ = false;
  }

  bool available() const {
    return this->source_ != nullptr && this->source_->HasData();
  }

  WeatherViewModel BuildView() const {
    WeatherViewModel v;
    ESPTime now{};
    const bool have_time = this->resolve_time_(now);
    if (this->source_ != nullptr && this->source_->HasData()) {
      const pixoo::WeatherData d = this->source_->Data();
      this->fill_from_data_(v, d, have_time ? now.hour : -1);
      if (have_time && d.has_location)
        v.sky = this->sky_state_(now.timestamp, d.latitude, d.longitude);
    }
    this->fill_time_(v, now, have_time);
    return v;
  }

 protected:
  void fill_from_data_(WeatherViewModel &v, const pixoo::WeatherData &d,
                       int current_hour) const {
    v.condition = d.condition;
    v.is_night = d.is_night;
    v.has_temperature = d.has_temperature;
    v.temperature = d.temperature;
    v.has_feels_like = d.has_feels_like;
    v.feels_like = d.feels_like;
    v.has_humidity = d.has_humidity;
    v.humidity = d.humidity;
    v.has_high = d.has_high;
    v.high = d.high;
    v.has_low = d.has_low;
    v.low = d.low;

    const pixoo::WeatherNow sel = pixoo::SelectWeatherNow(d, current_hour);
    // Roll the hero condition/temperature/feels-like/humidity to the current
    // hour from the buffered series, so they advance between fetches. Within
    // the hour the snapshot was fetched in, the snapshot's own current readings
    // are the live observation for that hour and the series entry is only the
    // forecast for its top, so the live values are kept.
    if (sel.has_current_hour && !sel.current_is_fetch_hour) {
      v.condition = sel.current.condition;
      v.is_night = sel.current.is_night;
      v.has_temperature = true;
      v.temperature = sel.current.temperature;
      v.has_feels_like = true;
      v.feels_like = sel.current.feels_like;
      v.has_humidity = true;
      v.humidity = sel.current.humidity;
    }
    for (int i = 0; i < kForecastHours; i++) {
      const pixoo::WeatherHourData &h = sel.columns[i];
      if (!h.valid)
        continue;
      v.hours[i].valid = true;
      v.hours[i].condition = h.condition;
      v.hours[i].is_night = h.is_night;
      v.hours[i].temperature = h.temperature;
      char lbl[8];
      int hh = h.hour_of_day % 24;
      std::snprintf(lbl, sizeof(lbl), "%d", hh == 0 ? 24 : hh);
      v.hours[i].label = lbl;
    }
  }

  bool resolve_time_(ESPTime &now) const {
    if (this->has_fixed_time_) {
      now = ESPTime::from_epoch_utc(this->fixed_epoch_);
      return now.is_valid();
    }
    if (this->time_ != nullptr) {
      now = this->time_->now();
      return now.is_valid();
    }
    return false;
  }

  const pixoo::SkyState &sky_state_(time_t epoch, float latitude,
                                     float longitude) const {
    if (!this->sky_cache_valid_ || this->sky_cache_epoch_ != epoch ||
        this->sky_cache_latitude_ != latitude ||
        this->sky_cache_longitude_ != longitude) {
      this->sky_cache_ =
          pixoo::ComputeSkyState(epoch, latitude, longitude);
      this->sky_cache_epoch_ = epoch;
      this->sky_cache_latitude_ = latitude;
      this->sky_cache_longitude_ = longitude;
      this->sky_cache_valid_ = true;
    }
    return this->sky_cache_;
  }

  void fill_time_(WeatherViewModel &v, ESPTime now, bool have_time) const {
    if (have_time) {
      char buf[24];
      now.strftime(buf, sizeof(buf), "%H:%M");
      v.time_text = buf;
      now.strftime(buf, sizeof(buf), "%a %d");
      v.date_text = buf;
    }
  }

  WeatherSource *source_{nullptr};
  time::RealTimeClock *time_{nullptr};
  time_t fixed_epoch_{0};
  bool has_fixed_time_{false};

  // BuildView runs for both Tick and Render. Cache the astronomy result within
  // each UTC second; weather values and formatted time remain live.
  mutable bool sky_cache_valid_{false};
  mutable time_t sky_cache_epoch_{0};
  mutable float sky_cache_latitude_{0.0f};
  mutable float sky_cache_longitude_{0.0f};
  mutable pixoo::SkyState sky_cache_{};
};

}  // namespace esphome::pixoo64::weather
