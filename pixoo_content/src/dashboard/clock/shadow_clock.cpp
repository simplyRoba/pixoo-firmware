#include "shadow_clock.h"

#include <algorithm>
#include <cmath>

namespace pixoo::clock {
namespace {

float WrapAngle(double degrees) {
  const float angle = static_cast<float>(std::fmod(degrees, 360.0));
  return angle < 360.0f ? angle : 0.0f;
}

}  // namespace

void ShadowClockModel::StartLoad(uint32_t now_ms) {
  this->Clear();
  this->loading_ = true;
  this->load_started_ms_ = now_ms;
}

void ShadowClockModel::Clear() { *this = ShadowClockModel{}; }

void ShadowClockModel::Update(int hour, int minute, int second,
                              uint32_t now_ms) {
  if (hour < 0 || hour > 23 || minute < 0 || minute > 59 ||
      second < 0 || second > 59) {
    this->Clear();
    return;
  }

  const int civil_second = hour * 3600 + minute * 60 + second;
  if (civil_second != this->civil_second_) {
    this->civil_second_ = civil_second;
    this->second_started_ms_ = now_ms;
  }
  this->has_time_ = true;

  // Unsigned differences preserve both timelines across millis rollover.
  const uint32_t elapsed = std::min(now_ms - this->second_started_ms_,
                                    uint32_t{1000});
  const double seconds = second + elapsed / 1000.0;
  this->angles_.second = WrapAngle(seconds * 6.0);
  this->angles_.minute = WrapAngle(minute * 6.0 + seconds / 10.0);
  this->angles_.hour = WrapAngle((hour % 12) * 30.0 + minute / 2.0 +
                                  seconds / 120.0);

  this->level_ = 1.0f;
  if (this->loading_) {
    const uint32_t elapsed_load = now_ms - this->load_started_ms_;
    if (elapsed_load >= kLoadMs) {
      this->loading_ = false;
    } else {
      const float t = static_cast<float>(elapsed_load) / kLoadMs;
      this->level_ = t * t * (3.0f - 2.0f * t);
    }
  }
}

}  // namespace pixoo::clock
