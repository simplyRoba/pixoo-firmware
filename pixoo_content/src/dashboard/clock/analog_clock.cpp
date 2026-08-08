#include "analog_clock.h"

#include <cmath>

namespace pixoo::clock {
namespace {

constexpr float kPi = 3.14159265358979323846f;

// Turns of the minute hand per turn of the hour hand. The wind drives the hour
// hand and derives the minute hand from it, so the two stay geared.
constexpr float kMinutesPerHourTurn = 12.0f;

int Wrap(int value, int modulus) {
  const int r = value % modulus;
  return r < 0 ? r + modulus : r;
}

float WrapAngle(float degrees) {
  const float r = std::fmod(degrees, 360.0f);
  return r < 0.0f ? r + 360.0f : r;
}

// Unsigned subtraction wraps, so this holds across the 32-bit millis rollover.
uint32_t Elapsed(uint32_t now_ms, uint32_t since_ms) { return now_ms - since_ms; }

// Degrees the second hand sits ahead of its step, `t` running 0..1 across the
// rebound. A damped sine, zero at both ends: the hand overshoots, swings back
// short, and settles.
float Rebound(float t) {
  return AnalogClockModel::kReboundDeg * std::exp(-3.0f * t) *
         std::sin(3.0f * kPi * t);
}

constexpr float kSettleSec = AnalogClockModel::kWindSettleMs / 1000.0f;

// Splits a wind of `distance` degrees at `speed` into its cruise and its
// deceleration. A wind too short to reach cruising speed is all deceleration,
// shortened to fit.
struct WindPhases {
  float cruise_deg;
  float cruise_sec;
  float settle_deg;
  float settle_sec;
};

WindPhases SplitWind(float distance, float speed) {
  const float settle_deg = speed * kSettleSec / 2.0f;
  if (distance >= settle_deg) {
    return WindPhases{distance - settle_deg, (distance - settle_deg) / speed,
                      settle_deg, kSettleSec};
  }
  return WindPhases{0.0f, 0.0f, distance, 2.0f * distance / speed};
}

uint32_t PhaseMs(const WindPhases &w) {
  return static_cast<uint32_t>((w.cruise_sec + w.settle_sec) * 1000.0f + 0.5f);
}

float HourTarget(int hour, int minute) {
  return (Wrap(hour, 24) % 12) * 30.0f + Wrap(minute, 60) * 0.5f;
}

float SecondTarget(int second) { return Wrap(second, 60) * 6.0f; }

}  // namespace

float AnalogClockModel::WindTravel(float target, float speed,
                                   uint32_t since_ms) {
  const WindPhases w = SplitWind(target, speed);
  const float t = since_ms / 1000.0f;
  if (t <= w.cruise_sec)
    return speed * t;
  if (w.settle_sec <= 0.0f)
    return target;
  const float s = t - w.cruise_sec;
  if (s >= w.settle_sec)
    return target;
  // Constant deceleration from the speed that covers settle_deg in settle_sec.
  const float settle_speed = 2.0f * w.settle_deg / w.settle_sec;
  return w.cruise_deg + settle_speed * s * (1.0f - s / (2.0f * w.settle_sec));
}

uint32_t AnalogClockModel::HandsWindMs(int hour, int minute) {
  return PhaseMs(SplitWind(HourTarget(hour, minute), kWindHandsDegPerSec));
}

float AnalogClockModel::SecondWindTarget(int second) {
  // The clock runs on while the hand travels, so the hand is aimed one step
  // further for every whole second the travel takes. The steps accumulate as
  // travel rather than wrapping, so passing 60 is another turn of the dial and
  // never a shorter trip.
  float target = SecondTarget(second);
  for (int i = 0; i < 4; i++) {
    const uint32_t ms = PhaseMs(SplitWind(target, kWindSecondDegPerSec));
    const float aimed =
        SecondTarget(second) + 6.0f * static_cast<float>(ms / 1000);
    if (aimed <= target)
      break;
    target = aimed;
  }
  return target;
}

uint32_t AnalogClockModel::SecondWindMs(int second) {
  return PhaseMs(SplitWind(SecondWindTarget(second), kWindSecondDegPerSec));
}

uint32_t AnalogClockModel::WindMs(int hour, int minute, int second) {
  const uint32_t hands = HandsWindMs(hour, minute);
  const uint32_t seconds = SecondWindMs(second);
  return hands > seconds ? hands : seconds;
}

uint32_t AnalogClockModel::WindMs() { return WindMs(11, 59, 59); }

float AnalogClockModel::SteppedSecond(int second, uint32_t now_ms) const {
  const uint32_t since_step = Elapsed(now_ms, this->step_started_ms_);
  float rebound = 0.0f;
  if (since_step < kReboundMs) {
    rebound = Rebound(static_cast<float>(since_step) /
                      static_cast<float>(kReboundMs));
  }
  return SecondTarget(second) + rebound;
}

void AnalogClockModel::StartWind(uint32_t now_ms) {
  this->winding_ = true;
  this->wind_started_ms_ = now_ms;
  this->wind_target_set_ = false;
  this->second_landed_ = false;
}

void AnalogClockModel::Park() {
  this->angles_ = HandAngles{};
  this->shown_second_ = -1;
}

void AnalogClockModel::Update(int hour, int minute, int second,
                              uint32_t now_ms) {
  hour = Wrap(hour, 24);
  minute = Wrap(minute, 60);
  second = Wrap(second, 60);

  if (second != this->shown_second_) {
    this->shown_second_ = second;
    this->step_started_ms_ = now_ms;
  }

  const float hour_target = HourTarget(hour, minute);

  if (this->winding_) {
    if (!this->wind_target_set_) {
      this->wind_hour_target_ = hour_target;
      this->wind_hands_ms_ = HandsWindMs(hour, minute);
      this->wind_second_ms_ = SecondWindMs(second);
      this->wind_second_target_ = SecondWindTarget(second);
      this->wind_target_set_ = true;
    }
    const uint32_t since_wind = Elapsed(now_ms, this->wind_started_ms_);

    if (since_wind < this->wind_second_ms_) {
      this->angles_.second = WindTravel(this->wind_second_target_,
                                        kWindSecondDegPerSec, since_wind);
    } else {
      // Landed: from here the hand ticks with the clock while the geared hands
      // may still be travelling.
      if (!this->second_landed_) {
        this->second_landed_ = true;
        this->step_started_ms_ = now_ms;
      }
      this->angles_.second = this->SteppedSecond(second, now_ms);
    }

    if (since_wind < this->wind_hands_ms_) {
      const float travel = WindTravel(this->wind_hour_target_,
                                      kWindHandsDegPerSec, since_wind);
      this->angles_.hour = travel;
      this->angles_.minute = WrapAngle(travel * kMinutesPerHourTurn);
      return;
    }
    this->winding_ = false;
  }

  this->angles_.hour = hour_target;
  this->angles_.minute = minute * 6.0f;
  this->angles_.second = this->SteppedSecond(second, now_ms);
}

}  // namespace pixoo::clock
