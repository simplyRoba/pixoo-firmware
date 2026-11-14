#pragma once

#include <cstdint>

#include "shadow_clock.h"
#include "watch_face.h"

namespace esphome::pixoo64::clockface {

namespace detail {
constexpr uint8_t kShadowSecondInk = 220;
// Pixel coverage of the foreground stroke; coordinates are Q8 hand-local pixels.
uint8_t ShadowSecondCoverage(int along, int across);
}  // namespace detail

// Finite monochrome light-emitting hands with continuous source visibility.
class ShadowFace : public WatchFace {
 public:
  ShadowFace();

  void Tick(const ClockTime &time, uint32_t now_ms) override;
  void OnShow(uint32_t now_ms) override;
  void Render(display::Display &display) const override;

 protected:
  pixoo::clock::ShadowClockModel model_;
  bool load_pending_{false};

};

}  // namespace esphome::pixoo64::clockface
