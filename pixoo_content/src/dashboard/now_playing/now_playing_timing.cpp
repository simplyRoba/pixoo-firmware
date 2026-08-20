#include "now_playing_timing.h"

namespace pixoo::now_playing {

int MarqueeTiming::Offset(uint64_t text_identity, int text_width, int viewport_width,
                          uint32_t now_ms, uint32_t pause_ms, uint32_t step_ms,
                          int gap_px) {
  if (!initialized_ || identity_ != text_identity) {
    initialized_ = true; identity_ = text_identity; started_ms_ = now_ms;
  }
  if (text_width <= viewport_width || step_ms == 0) return 0;
  const int gap = gap_px < 0 ? 0 : gap_px;
  const uint32_t elapsed = now_ms - started_ms_;
  if (elapsed < pause_ms) return 0;
  const uint32_t steps = (elapsed - pause_ms) / step_ms;
  const uint32_t loop = static_cast<uint32_t>(text_width + gap);
  return loop == 0 ? 0 : static_cast<int>(steps % loop);
}

void TransitionTimeline::Start(uint32_t now_ms, uint32_t duration_ms) {
  started_ms_ = now_ms; duration_ms_ = duration_ms; started_ = true;
}

uint8_t TransitionTimeline::Linear(uint32_t now_ms) const {
  if (!started_ || duration_ms_ == 0) return 255;
  const uint32_t elapsed = now_ms - started_ms_;
  if (elapsed >= duration_ms_) return 255;
  return static_cast<uint8_t>((static_cast<uint64_t>(elapsed) * 255u) / duration_ms_);
}

uint8_t TransitionTimeline::Smooth(uint32_t now_ms) const {
  const uint32_t x = Linear(now_ms);
  // smoothstep x*x*(3-2*x), in a 0..255 fixed-point domain.
  return static_cast<uint8_t>((static_cast<uint64_t>(x) * x * (765u - 2u * x)) / (255u * 255u));
}

bool TransitionTimeline::Complete(uint32_t now_ms) const {
  return !started_ || duration_ms_ == 0 || now_ms - started_ms_ >= duration_ms_;
}

}  // namespace pixoo::now_playing
