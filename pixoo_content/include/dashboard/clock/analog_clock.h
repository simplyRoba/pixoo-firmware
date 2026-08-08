#pragma once

#include <cstdint>

namespace pixoo {

// Analog watch movement for an HH:MM:SS display: hand positions only, no dial
// geometry and no pixels.
namespace clock {

// Degrees clockwise from 12 o'clock. An angle leaves [0, 360) while the second
// hand rebounds around a step and while the movement winds into place.
struct HandAngles {
  float hour{0.0f};
  float minute{0.0f};
  float second{0.0f};
};

// A quartz movement: the second hand jumps a whole step per second and rebounds
// around it, the minute hand steps per minute, and the hour hand creeps with
// the minutes.
class AnalogClockModel {
 public:
  // Rebound of the second hand after a step: the amplitude of its overshoot
  // past the step and the time it takes to settle on it. In between the hand
  // swings back short of the step, by about a quarter of the overshoot.
  static constexpr float kReboundDeg = 4.5f;
  static constexpr uint32_t kReboundMs = 130;

  // Winding the movement forward from 12:00 to the time it should show, as a
  // clock setting itself does. Every hand leaves 12 at once and runs at its
  // own constant speed, decelerating to rest over its last kWindSettleMs, so
  // each arrives when its own distance is covered.
  //
  // The hour and minute hands stay geared 1:12 throughout, so their motion is
  // one the gear train could make on its own, and the hour hand runs at
  // kWindHandsDegPerSec. The second hand cannot hold that gearing and runs at
  // kWindSecondDegPerSec; being the faster hand it usually lands first, and
  // from then on it ticks normally while the other two are still travelling.
  //
  // A wind covers real distance, so it takes as long as that distance demands:
  // longest from a time just short of 12, and barely there at all just past it.
  static constexpr float kWindHandsDegPerSec = 70.0f;
  static constexpr float kWindSecondDegPerSec = 150.0f;
  static constexpr uint32_t kWindSettleMs = 400;

  // Travel of the geared hands and of the second hand, the whole wind onto
  // `hour`:`minute`:`second`, and the longest wind.
  static uint32_t HandsWindMs(int hour, int minute);
  static uint32_t SecondWindMs(int second);
  static uint32_t WindMs(int hour, int minute, int second);
  static uint32_t WindMs();

  // Winds from 12 o'clock onto the time the next Update() reports.
  void StartWind(uint32_t now_ms);
  bool winding() const { return this->winding_; }

  // Rests every hand at 12 o'clock, where a wind starts from. A movement with
  // no time to show sits here, motionless.
  void Park();

  // Fields outside their unit are taken modulo it, negatives included.
  void Update(int hour, int minute, int second, uint32_t now_ms);

  HandAngles Angles() const { return this->angles_; }

 private:
  // Distance covered `since_ms` into a wind of `target` degrees at `speed`.
  static float WindTravel(float target, float speed, uint32_t since_ms);
  // Degrees the second hand travels to land on the second showing when it
  // arrives, rather than the one showing when it set off.
  static float SecondWindTarget(int second);
  // Angle of the second hand at `now_ms`, stepping and rebounding on the
  // second it currently shows.
  float SteppedSecond(int second, uint32_t now_ms) const;

  HandAngles angles_{};
  // Start of the current second, and of the wind. Unsigned subtraction wraps,
  // so both hold across the 32-bit millis rollover.
  uint32_t step_started_ms_{0};
  uint32_t wind_started_ms_{0};
  bool winding_{false};
  bool second_landed_{false};
  // Latched at the first Update() of a wind, so time moving on midway cannot
  // change the distance the hands are already covering. The second hand is
  // aimed one step further for every second that passes while it travels, so
  // it lands on the second actually being shown.
  float wind_hour_target_{0.0f};
  float wind_second_target_{0.0f};
  uint32_t wind_hands_ms_{0};
  uint32_t wind_second_ms_{0};
  bool wind_target_set_{false};
  int shown_second_{-1};
};

}  // namespace clock
}  // namespace pixoo
