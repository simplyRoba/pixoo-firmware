#pragma once

#include <cstdint>

#include "analog_clock.h"
#include "esphome/components/display/display.h"
#include "watch_face.h"

namespace esphome::pixoo64::clockface {

// Full-screen rounded-rectangle dial: a marker for every minute, longer hour
// markers, tapered hour and minute hands, and a red second hand. Every element
// is anti-aliased from its coverage of each pixel, so an edge at any angle
// reads as straight rather than as a staircase.
class AnalogFace : public WatchFace {
 public:
  void Tick(const ClockTime &time, uint32_t now_ms) override;
  // The movement winds forward out of 12 into position, so the face sets
  // itself each time it is shown.
  void OnShow(uint32_t now_ms) override;
  void Render(display::Display &display) const override;

 protected:
  pixoo::clock::AnalogClockModel model_;
  bool wind_pending_{false};
};

}  // namespace esphome::pixoo64::clockface
