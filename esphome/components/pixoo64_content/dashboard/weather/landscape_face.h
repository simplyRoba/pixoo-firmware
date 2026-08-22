#pragma once

#include <cstdint>

#include "esphome/components/display/display.h"
#include "weather_effects.h"
#include "weather_face.h"

namespace esphome::pixoo64::dashboard::weather {

// Landscape weather face. By day it draws a sky over a horizon with the sun
// riding an arc from sunrise to sunset; by night a starfield with the moon at
// its current phase. Clouds respond to the current condition and pass in front
// of celestial bodies; stars twinkle and the sun glow pulses. Tick() stores the
// presentation time; Render() is const and reads no clock, source, or location.
class LandscapeFace : public WeatherFace {
 public:
  void OnShow(uint32_t now_ms) override;
  void Tick(const WeatherViewModel &view, uint32_t now_ms) override;
  void Render(display::Display &display, const WeatherViewModel &view,
              const WeatherFonts &fonts) const override;

 private:
  pixoo::WeatherAnimationClock effect_clock_;
  pixoo::WeatherTransitionState effects_;
};

}  // namespace esphome::pixoo64::dashboard::weather
