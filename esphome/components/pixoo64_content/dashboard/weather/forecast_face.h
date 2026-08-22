#pragma once

#include <cstdint>

#include "esphome/components/display/display.h"
#include "weather_effects.h"
#include "weather_face.h"
#include "weather_renderer.h"

namespace esphome::pixoo64::dashboard::weather {

// Forecast weather face with clock, date, current conditions, and hourly
// forecast.
class ForecastFace : public WeatherFace {
 public:
  void OnShow(uint32_t now_ms) override { this->clock_.Resume(now_ms); }

  void Tick(const WeatherViewModel &view, uint32_t now_ms) override {
    this->clock_.Tick(now_ms);
    this->storm_ = pixoo::WeatherEffectFor(view.condition).storm;
  }

  void Render(display::Display &display, const WeatherViewModel &view,
              const WeatherFonts &fonts) const override {
    ::esphome::pixoo64::weather::RenderWeather(display, view, fonts,
                                               this->Animation_());
  }
 protected:
  // Lightning follows the same bounded schedule the landscape scene runs on,
  // so a strike is an isolated event rather than a blink.
  ::esphome::pixoo64::weather::IconAnimation Animation_() const {
    const uint64_t elapsed = this->clock_.elapsed_ms();
    const pixoo::LightningState lightning =
        pixoo::LightningAt(this->storm_, elapsed);
    return {elapsed, lightning.intensity /
                         static_cast<float>(pixoo::kLightningMaxIntensity)};
  }

  pixoo::WeatherAnimationClock clock_;
  bool storm_{false};
};

}  // namespace esphome::pixoo64::dashboard::weather
