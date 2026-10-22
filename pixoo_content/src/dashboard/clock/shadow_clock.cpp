#include "shadow_clock.h"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace pixoo::clock {
namespace {

struct IgnitionKey {
  uint32_t ms;
  float level;
};

constexpr IgnitionKey kHourIgnition[] = {
    {0, 0.0f}, {120, 0.06f}, {230, 0.12f}, {280, 0.78f},
    {340, 0.10f}, {500, 0.90f}, {650, 0.94f}, {700, 0.20f},
    {840, 1.0f}, {930, 0.70f}, {1050, 1.0f},
    {ShadowClockModel::kLoadMs, 1.0f}};
constexpr IgnitionKey kMinuteIgnition[] = {
    {0, 0.0f}, {170, 0.04f}, {280, 0.10f}, {350, 0.74f},
    {420, 0.12f}, {600, 0.94f}, {740, 0.96f}, {810, 0.26f},
    {950, 1.0f}, {1030, 0.78f}, {1150, 1.0f},
    {ShadowClockModel::kLoadMs, 1.0f}};
constexpr uint32_t kSecondFadeMs = 300;
constexpr uint32_t kSecondIgnitionMs = ShadowClockModel::kLoadMs - kSecondFadeMs;

float Smoothstep(float t) { return t * t * (3.0f - 2.0f * t); }

template <std::size_t N>
float IgnitionLevel(const IgnitionKey (&keys)[N], uint32_t elapsed) {
  for (std::size_t i = 1; i < N; ++i) {
    if (elapsed <= keys[i].ms) {
      const auto &a = keys[i - 1];
      const auto &b = keys[i];
      const float t = static_cast<float>(elapsed - a.ms) / (b.ms - a.ms);
      return a.level + (b.level - a.level) * Smoothstep(t);
    }
  }
  return keys[N - 1].level;
}

float WrapAngle(double degrees) {
  const float angle = static_cast<float>(std::fmod(degrees, 360.0));
  return angle < 360.0f ? angle : 0.0f;
}

}  // namespace

float ShadowClockModel::Level() const {
  return std::max(this->levels_.hour,
                  std::max(this->levels_.minute, this->levels_.second));
}

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

  this->levels_ = {1.0f, 1.0f, 1.0f};
  if (this->loading_) {
    const uint32_t elapsed_load = now_ms - this->load_started_ms_;
    if (elapsed_load >= kLoadMs) {
      this->loading_ = false;
    } else {
      this->levels_ = {
          IgnitionLevel(kHourIgnition, elapsed_load),
          IgnitionLevel(kMinuteIgnition, elapsed_load),
          elapsed_load <= kSecondIgnitionMs ? 0.0f :
              Smoothstep(static_cast<float>(elapsed_load - kSecondIgnitionMs) /
                         kSecondFadeMs)};
    }
  }
}

}  // namespace pixoo::clock
