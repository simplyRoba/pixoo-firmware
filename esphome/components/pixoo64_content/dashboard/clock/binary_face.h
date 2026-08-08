#pragma once

#include <cstdint>

#include "binary_clock.h"
#include "esphome/components/display/display.h"
#include "watch_face.h"

namespace esphome::pixoo64::clockface {

// Binary-coded-decimal clock: six columns of dots for HH:MM:SS, one column per
// decimal digit, read bottom-up as 1-2-4-8. Hours are amber, minutes white,
// seconds red; the hour/minute and minute/second column pairs are set wider
// apart than the two columns within a pair, so the three groups read without
// separators. Positions a digit can never reach carry no dot; the rest show a
// dim placeholder so the layout stays legible when unlit. A dot lighting up
// flares brighter and wider than its resting state before settling, one going
// out fades down, and a carry runs right to left across the columns.
class BinaryFace : public WatchFace {
 public:
  void Tick(const ClockTime &time, uint32_t now_ms) override;
  // The columns sweep in from unlit, so the face loads itself each time it is
  // shown.
  void OnShow(uint32_t now_ms) override;
  void Render(display::Display &display) const override;

 protected:
  pixoo::clock::BinaryClockModel model_;
  bool load_pending_{false};
};

}  // namespace esphome::pixoo64::clockface
