#pragma once

#include <cstdint>

#include "esphome/components/display/display.h"

namespace esphome::pixoo64::dashboard::equalizer {

constexpr int kBars = 16;

// Immutable view of one equalizer frame: per-bar current level and peak-hold
// marker, each normalized to [0, 1]. Built by the dashboard from the smoothed
// microphone levels, drawn by a face (no sensor/FFT access here, so a face is
// host-testable).
struct EqualizerView {
  float level[kBars];
  float peak[kBars];
};

// One presentation of the equalizer dashboard. Tick() advances any per-face
// animation state (e.g. trailing echoes) to `now_ms`; Render() draws the
// current view. The face is bound per dashboard instance at composition time,
// so each face is its own selectable dashboard.
class EqualizerFace {
 public:
  virtual ~EqualizerFace() = default;
  virtual void Tick(const EqualizerView &view, uint32_t now_ms) {
    (void) view;
    (void) now_ms;
  }
  // Called when the equalizer dashboard becomes visible, before the next Tick().
  virtual void OnShow(uint32_t now_ms) { (void) now_ms; }
  virtual void Render(display::Display &display,
                      const EqualizerView &view) const = 0;
};

}  // namespace esphome::pixoo64::dashboard::equalizer
