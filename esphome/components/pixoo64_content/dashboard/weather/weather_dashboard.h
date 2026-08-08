#pragma once

#include "dashboard/dashboard.h"
#include "forecast_face.h"
#include "landscape_face.h"
#include "esphome/components/display/display.h"
#include "esphome/components/font/font.h"
#include "esphome/components/time/real_time_clock.h"
#include "weather_face.h"
#include "weather_model.h"

namespace esphome::pixoo64::dashboard {

using weather::WeatherFace;
using weather::WeatherFonts;

// Weather dashboard. It gathers values from the WeatherSource port plus the
// clock (the WeatherModel), builds an immutable view, and hands that to one
// face, which owns the drawing. The face is bound per dashboard instance at
// composition time, so each face is its own selectable dashboard.
class WeatherDashboard : public Dashboard {
 public:
  void set_font_small(font::Font *f) { this->fonts_.small = f; }
  void set_font_big(font::Font *f) { this->fonts_.big = f; }
  void set_time(time::RealTimeClock *t) { this->weather_.set_time(t); }
  void set_fixed_time(time_t epoch) { this->weather_.set_fixed_time(epoch); }
  void set_source(pixoo::WeatherSource *source) {
    this->weather_.set_source(source);
  }
  void set_face(WeatherFace *face) { this->face_ = face; }

  bool available() const override {
    if (this->fonts_.small == nullptr || this->fonts_.big == nullptr)
      return false;
    if (this->face_ == nullptr)
      return false;
    // The dashboard is available while its source is still loading, so it can
    // show a loading animation instead of nothing.
    return this->weather_.loading() || this->weather_.available();
  }

  void OnShow(uint32_t now_ms) override {
    if (this->face_ != nullptr)
      this->face_->OnShow(now_ms);
  }

  // Called each render tick while this dashboard is visible; drives the
  // demand-based fetch so a source only fetches while its weather is shown, and
  // advances the face's animation.
  void Tick(uint32_t now_ms) override {
    this->weather_.RequestRefresh();
    // Do not make the loading placeholder an observed condition: the first
    // source condition must enter the face settled rather than cross-fading
    // from UNKNOWN.
    if (this->face_ != nullptr && this->weather_.available())
      this->face_->Tick(this->weather_.BuildView(), now_ms);
  }

  void Render(display::Display &display) const override {
    if (this->weather_.loading()) {
      this->face_->RenderLoading(display, this->fonts_);
      return;
    }
    this->face_->Render(display, this->weather_.BuildView(), this->fonts_);
  }

 protected:
  WeatherFonts fonts_;
  ::esphome::pixoo64::weather::WeatherModel weather_;
  WeatherFace *face_{nullptr};
};

class ForecastWeatherDashboard : public WeatherDashboard {
 public:
  ForecastWeatherDashboard() { this->set_face(&this->face_impl_); }

 protected:
  weather::ForecastFace face_impl_;
};

class LandscapeWeatherDashboard : public WeatherDashboard {
 public:
  LandscapeWeatherDashboard() { this->set_face(&this->face_impl_); }

 protected:
  weather::LandscapeFace face_impl_;
};

}  // namespace esphome::pixoo64::dashboard
