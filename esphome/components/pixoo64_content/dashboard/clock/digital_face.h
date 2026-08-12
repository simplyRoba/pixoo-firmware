#pragma once

#include <cstdint>

#include "digital_clock.h"
#include "esphome/components/display/display.h"
#include "watch_face.h"

namespace esphome::pixoo64::clockface {

// Rounded seven-segment HH:MM clock. Segment changes fade and flare through a
// fixed-size animation model; no inactive segment outlines are drawn.
class DigitalFace : public WatchFace {
 public:
  void Tick(const ClockTime &time, uint32_t now_ms) override;
  // The four digits sweep in from left to right each time the face is shown.
  void OnShow(uint32_t now_ms) override;
  void Render(display::Display &display) const override;

 protected:
  pixoo::clock::DigitalClockModel model_;
  bool load_pending_{false};
};

}  // namespace esphome::pixoo64::clockface
