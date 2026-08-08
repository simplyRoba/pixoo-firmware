#pragma once

#include <cstdint>

namespace pixoo {

// Split-flap column mechanics for a HH:MM display: hour tens, hour ones,
// minute tens, minute ones. A column steps one flap at a time toward its
// target, riffling through every digit in between.
namespace clock {

constexpr int kFlapCount = 4;

// `phase` runs 0..1 across the single flap from `from` to `to`. A settled
// column reports from == to, phase 0, flipping false.
struct FlapView {
  int from{0};
  int to{0};
  float phase{0.0f};
  bool flipping{false};
};

// Out-of-range values yield -1.
void FlapDigits(int hour, int minute, int digits[kFlapCount]);

class SplitFlapModel {
 public:
  static constexpr int kDigits = 10;
  // A column spins up as its run goes on: the first flap takes kStepMs and
  // each following one is kRampFactor of the last, down to kFastStepMs. A
  // single-flap minute change stays slow; a long riffle accelerates.
  static constexpr uint32_t kStepMs = 350;
  static constexpr uint32_t kFastStepMs = 200;
  static constexpr float kRampFactor = 0.88f;

  // Duration of flap `index` within a run, and of a whole run of `flaps`.
  static uint32_t StepMs(int index);
  static uint32_t RunMs(int flaps);
  // Columns that start together do so a beat apart, left to right, so the
  // board clatters across instead of moving in lockstep. A column starting
  // alone is not delayed.
  static constexpr uint32_t kStaggerMs = 120;
  // A real board's units re-seek on their own. With every column settled, all
  // columns run a staggered full revolution this far apart.
  static constexpr uint32_t kIdleMinMs = 180000;
  static constexpr uint32_t kIdleMaxMs = 300000;

  // Digits outside 0..9 leave their column's target unchanged. Columns start
  // showing 0, so the first update riffles up from 00:00.
  void Update(const int digits[kFlapCount], uint32_t now_ms);
  // Queues a full revolution on every column, landing each on its target.
  void SpinAll(uint32_t now_ms);

  FlapView View(int index) const;

 private:
  struct Column {
    int shown{0};
    int target{0};
    int flaps_left{0};
    int flaps_total{0};
    bool moving{false};
    float phase{0.0f};
    // May be in the future while a staggered start is pending.
    uint32_t step_started_ms{0};
  };

  void Start_(int index, int flaps, uint32_t now_ms, uint32_t delay_ms);
  static void Retarget_(Column &column, int target);
  void ScheduleIdle_(uint32_t now_ms);
  bool AnyFlipping_() const;
  uint32_t NextRandom_();
  static void Advance_(Column &column, uint32_t now_ms);

  Column columns_[kFlapCount];
  uint32_t rng_{0x9E3779B9};
  uint32_t next_idle_ms_{0};
  bool idle_armed_{false};
};

}  // namespace clock
}  // namespace pixoo
