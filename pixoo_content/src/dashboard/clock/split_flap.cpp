#include "split_flap.h"

namespace pixoo::clock {
namespace {

int NextDigit(int digit) { return (digit + 1) % SplitFlapModel::kDigits; }

// Flaps from `shown` up to `target`, always forward: a flap only falls one way.
int Distance(int shown, int target) {
  return (target - shown + SplitFlapModel::kDigits) % SplitFlapModel::kDigits;
}

// Unsigned subtraction wraps, so this holds across the 32-bit millis rollover.
uint32_t Elapsed(uint32_t now_ms, uint32_t since_ms) {
  return now_ms - since_ms;
}

// True while `deadline_ms` is still ahead of `now_ms`, tolerating the rollover
// for deadlines within half the counter's range.
bool Pending(uint32_t now_ms, uint32_t deadline_ms) {
  return static_cast<int32_t>(now_ms - deadline_ms) < 0;
}

}  // namespace

void FlapDigits(int hour, int minute, int digits[kFlapCount]) {
  const bool hour_valid = hour >= 0 && hour <= 23;
  const bool minute_valid = minute >= 0 && minute <= 59;
  digits[0] = hour_valid ? hour / 10 : -1;
  digits[1] = hour_valid ? hour % 10 : -1;
  digits[2] = minute_valid ? minute / 10 : -1;
  digits[3] = minute_valid ? minute % 10 : -1;
}

uint32_t SplitFlapModel::NextRandom_() {
  this->rng_ ^= this->rng_ << 13;
  this->rng_ ^= this->rng_ >> 17;
  this->rng_ ^= this->rng_ << 5;
  return this->rng_;
}

uint32_t SplitFlapModel::StepMs(int index) {
  float step = kStepMs;
  for (int i = 0; i < index; i++) {
    step *= kRampFactor;
    if (step <= kFastStepMs)
      return kFastStepMs;
  }
  return static_cast<uint32_t>(step + 0.5f);
}

uint32_t SplitFlapModel::RunMs(int flaps) {
  uint32_t total = 0;
  for (int i = 0; i < flaps; i++)
    total += StepMs(i);
  return total;
}

void SplitFlapModel::Advance_(Column &column, uint32_t now_ms) {
  while (column.flaps_left > 0) {
    if (Pending(now_ms, column.step_started_ms)) {
      column.phase = 0.0f;
      column.moving = false;
      return;
    }
    column.moving = true;
    const uint32_t step = StepMs(column.flaps_total - column.flaps_left);
    const uint32_t elapsed = Elapsed(now_ms, column.step_started_ms);
    if (elapsed < step) {
      column.phase = static_cast<float>(elapsed) / static_cast<float>(step);
      return;
    }
    // Consume the elapsed time one flap at a time, so a starved tick replays
    // the skipped flaps instead of jumping the riffle.
    column.shown = NextDigit(column.shown);
    column.step_started_ms += step;
    column.flaps_left--;
  }
  column.phase = 0.0f;
  column.moving = false;
}

void SplitFlapModel::Start_(int index, int flaps, uint32_t now_ms,
                            uint32_t delay_ms) {
  Column &column = this->columns_[index];
  column.flaps_left = flaps;
  column.flaps_total = flaps;
  column.phase = 0.0f;
  column.moving = false;
  column.step_started_ms = now_ms + delay_ms;
}

void SplitFlapModel::Retarget_(Column &column, int target) {
  if (target == column.target)
    return;
  column.target = target;
  if (column.flaps_left == 0)
    return;

  const int completed = column.flaps_total - column.flaps_left;
  int remaining = Distance(column.shown, target);
  // Once a leaf has started falling it cannot reverse back to the shown digit.
  if (column.moving && remaining == 0)
    remaining = kDigits;
  column.flaps_left = remaining;
  column.flaps_total = completed + remaining;
  if (remaining == 0) {
    column.phase = 0.0f;
    column.moving = false;
  }
}

bool SplitFlapModel::AnyFlipping_() const {
  for (const Column &column : this->columns_) {
    if (column.flaps_left > 0)
      return true;
  }
  return false;
}

void SplitFlapModel::ScheduleIdle_(uint32_t now_ms) {
  const uint32_t span = kIdleMaxMs - kIdleMinMs;
  this->next_idle_ms_ = now_ms + kIdleMinMs + this->NextRandom_() % span;
  this->idle_armed_ = true;
}

void SplitFlapModel::SpinAll(uint32_t now_ms) {
  // A column travels to its target and then all the way around, so it lands on
  // the digit it will show having passed every other one.
  for (int i = 0; i < kFlapCount; i++)
    this->Start_(i, Distance(this->columns_[i].shown, this->columns_[i].target) +
                        kDigits,
                 now_ms, static_cast<uint32_t>(i) * kStaggerMs);
  this->ScheduleIdle_(now_ms);
}

void SplitFlapModel::Update(const int digits[kFlapCount], uint32_t now_ms) {
  if (!this->idle_armed_)
    this->ScheduleIdle_(now_ms);

  // Land any column whose flaps are already due, so one that just finished can
  // take off again in this same tick.
  for (Column &column : this->columns_)
    Advance_(column, now_ms);

  int starting = 0;
  for (int i = 0; i < kFlapCount; i++) {
    Column &column = this->columns_[i];
    if (digits[i] >= 0 && digits[i] <= 9)
      Retarget_(column, digits[i]);

    if (column.flaps_left == 0 && column.shown != column.target) {
      this->Start_(i, Distance(column.shown, column.target), now_ms,
                   static_cast<uint32_t>(starting) * kStaggerMs);
      starting++;
    }
  }

  for (Column &column : this->columns_)
    Advance_(column, now_ms);

  // The minute changes far more often than the idle interval, so the idle
  // re-seek is not deferred by a content change: it only waits for the board
  // to be still, and reschedules from the moment it fires.
  if (!this->AnyFlipping_() && !Pending(now_ms, this->next_idle_ms_)) {
    this->SpinAll(now_ms);
    for (Column &column : this->columns_)
      Advance_(column, now_ms);
  }
}

FlapView SplitFlapModel::View(int index) const {
  if (index < 0 || index >= kFlapCount)
    return FlapView{};
  const Column &column = this->columns_[index];
  FlapView view;
  view.from = column.shown;
  view.to = column.moving ? NextDigit(column.shown) : column.shown;
  view.phase = column.moving ? column.phase : 0.0f;
  view.flipping = column.moving;
  return view;
}

}  // namespace pixoo::clock
