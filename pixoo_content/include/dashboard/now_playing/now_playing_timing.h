#pragma once

#include <cstdint>

namespace pixoo::now_playing {

class MarqueeTiming {
 public:
  // `text_identity` resets the fixed animation state when the displayed row
  // changes. text_width and viewport_width are pixels.
  int Offset(uint64_t text_identity, int text_width, int viewport_width,
             uint32_t now_ms, uint32_t pause_ms, uint32_t step_ms, int gap_px);
  uint32_t CompletedCycles(uint64_t text_identity, int text_width,
                           int viewport_width, uint32_t now_ms,
                           uint32_t pause_ms, uint32_t step_ms,
                           int gap_px) const;
  void Delay(uint32_t duration_ms);

 private:
  uint64_t identity_{0};
  uint32_t started_ms_{0};
  bool initialized_{false};
};

class TransitionTimeline {
 public:
  void Start(uint32_t now_ms, uint32_t duration_ms);
  uint8_t Linear(uint32_t now_ms) const;
  uint8_t Smooth(uint32_t now_ms) const;
  bool Complete(uint32_t now_ms) const;

 private:
  uint32_t started_ms_{0};
  uint32_t duration_ms_{0};
  bool started_{false};
};

}  // namespace pixoo::now_playing
