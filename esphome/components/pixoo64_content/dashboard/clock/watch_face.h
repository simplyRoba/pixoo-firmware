#pragma once

#include <cstdint>

#include "esphome/components/display/display.h"

namespace esphome::pixoo64::clockface {

// `valid` is false until the clock has a real time; a face shows its unset
// state then.
struct ClockTime {
  int hour{0};
  int minute{0};
  int second{0};
  bool valid{false};
};

// One watch face of the clock dashboard. Tick() advances animation state to
// `now_ms`; Render() draws that state.
class WatchFace {
 public:
  virtual ~WatchFace() = default;
  virtual void Tick(const ClockTime &time, uint32_t now_ms) = 0;
  // Called when the clock dashboard becomes visible, before the next Tick().
  virtual void OnShow(uint32_t now_ms) { (void) now_ms; }
  virtual void Render(display::Display &display) const = 0;
};

}  // namespace esphome::pixoo64::clockface
