#pragma once

#include <algorithm>
#include <cstdint>

namespace pixoo {

struct FrameMetricsSnapshot {
  float average_ms{0.0f};
  float maximum_ms{0.0f};
  float frames_per_second{0.0f};
  uint32_t frames{0};
  uint32_t elapsed_ms{0};
};

// Accumulates frame timings over a caller-defined, millis()-based window.
// Unsigned timestamp subtraction remains valid across the uint32_t wrap.
class FrameMetricsWindow {
 public:
  void Reset(uint32_t now_ms) {
    this->started_ = true;
    this->started_ms_ = now_ms;
    this->frames_ = 0;
    this->total_us_ = 0;
    this->maximum_us_ = 0;
  }

  bool IsDue(uint32_t now_ms, uint32_t window_ms) const {
    return this->started_ && now_ms - this->started_ms_ >= window_ms;
  }

  void Record(uint32_t elapsed_us) {
    if (!this->started_)
      return;
    ++this->frames_;
    this->total_us_ += elapsed_us;
    this->maximum_us_ = std::max(this->maximum_us_, elapsed_us);
  }

  // Returns false until a nonzero-duration window has been established. On a
  // successful close, starts the next window at now_ms.
  bool Close(uint32_t now_ms, FrameMetricsSnapshot *snapshot) {
    if (snapshot == nullptr || !this->started_)
      return false;
    const uint32_t elapsed_ms = now_ms - this->started_ms_;
    if (elapsed_ms == 0)
      return false;

    snapshot->frames = this->frames_;
    snapshot->elapsed_ms = elapsed_ms;
    snapshot->average_ms =
        this->frames_ == 0
            ? 0.0f
            : static_cast<float>(this->total_us_) /
                  static_cast<float>(this->frames_) / 1000.0f;
    snapshot->maximum_ms = this->maximum_us_ / 1000.0f;
    snapshot->frames_per_second =
        static_cast<float>(this->frames_) * 1000.0f /
        static_cast<float>(elapsed_ms);
    this->Reset(now_ms);
    return true;
  }

 private:
  bool started_{false};
  uint32_t started_ms_{0};
  uint32_t frames_{0};
  uint64_t total_us_{0};
  uint32_t maximum_us_{0};
};

}  // namespace pixoo
