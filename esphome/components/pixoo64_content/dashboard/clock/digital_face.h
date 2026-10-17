#pragma once

#include <cstdint>

#include "digital_clock.h"
#include "esphome/components/display/display.h"
#include "watch_face.h"

namespace esphome::pixoo64::clockface {

// Condensed HH:MM numerals inside a rounded-square seconds wake.
class DigitalFace : public WatchFace {
 public:
  DigitalFace();
  void Tick(const ClockTime &time, uint32_t now_ms) override;
  // A clockwise perimeter wake accompanies the centered time's fade-in.
  void OnShow(uint32_t now_ms) override;
  void Render(display::Display &display) const override;

 protected:
  pixoo::clock::DigitalClockModel model_;
  bool load_pending_{false};
};

}  // namespace esphome::pixoo64::clockface
