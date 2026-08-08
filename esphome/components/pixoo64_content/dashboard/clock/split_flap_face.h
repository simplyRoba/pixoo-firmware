#pragma once

#include <cstdint>

#include "esphome/components/display/display.h"
#include "split_flap.h"
#include "watch_face.h"

namespace esphome::pixoo64::clockface {

// Solari-style departure-board face: four flap cards in two rows, hours above
// minutes. Each card drops its upper leaf one digit at a time, so a card
// riffles through every digit between the old and the new one. Digits are drawn
// from a built-in bitmap rather than a font, because the drop squashes them
// vertically per pixel.
class SplitFlapFace : public WatchFace {
 public:
  void Tick(const ClockTime &time, uint32_t now_ms) override;
  // Every column runs a full revolution into place, so the board replays its
  // clatter each time the face is shown.
  void OnShow(uint32_t now_ms) override;
  void Render(display::Display &display) const override;

 protected:
  pixoo::clock::SplitFlapModel model_;
  bool has_time_{false};
  bool spin_pending_{false};
};

}  // namespace esphome::pixoo64::clockface
