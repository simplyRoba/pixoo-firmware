#pragma once

#include <cstdint>

#include "esphome/components/display/display.h"
#include "weather_renderer.h"

namespace esphome::pixoo64::dashboard::weather {

using ::esphome::pixoo64::weather::WeatherFonts;
using ::esphome::pixoo64::weather::WeatherViewModel;

// One presentation of the weather dashboard. The dashboard gathers data and
// builds an immutable WeatherViewModel; a face owns only the drawing, so it is
// host-testable and has no source or clock access. Tick() advances any per-face
// animation state to `now_ms`; Render() draws the current view. The face is
// bound per dashboard instance at composition time, so each face is its own
// selectable dashboard.
class WeatherFace {
 public:
  virtual ~WeatherFace() = default;
  virtual void Tick(const WeatherViewModel &view, uint32_t now_ms) {
    (void) view;
    (void) now_ms;
  }
  // Called when the weather dashboard becomes visible, before the next Tick().
  virtual void OnShow(uint32_t now_ms) { (void) now_ms; }
  // Draw the loaded view.
  virtual void Render(display::Display &display,
                      const WeatherViewModel &view,
                      const WeatherFonts &fonts) const = 0;
  // Draw the placeholder shown while the source has no data yet.
  virtual void RenderLoading(display::Display &display,
                             const WeatherFonts &fonts) const = 0;
};

}  // namespace esphome::pixoo64::dashboard::weather
