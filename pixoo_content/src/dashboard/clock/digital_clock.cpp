#include "digital_clock.h"

namespace pixoo::clock {
namespace {

constexpr uint8_t kDigitSegments[10] = {
    kDigitalSegmentA | kDigitalSegmentB | kDigitalSegmentC |
        kDigitalSegmentD | kDigitalSegmentE | kDigitalSegmentF,
    kDigitalSegmentB | kDigitalSegmentC,
    kDigitalSegmentA | kDigitalSegmentB | kDigitalSegmentD |
        kDigitalSegmentE | kDigitalSegmentG,
    kDigitalSegmentA | kDigitalSegmentB | kDigitalSegmentC |
        kDigitalSegmentD | kDigitalSegmentG,
    kDigitalSegmentB | kDigitalSegmentC | kDigitalSegmentF |
        kDigitalSegmentG,
    kDigitalSegmentA | kDigitalSegmentC | kDigitalSegmentD |
        kDigitalSegmentF | kDigitalSegmentG,
    kDigitalSegmentA | kDigitalSegmentC | kDigitalSegmentD |
        kDigitalSegmentE | kDigitalSegmentF | kDigitalSegmentG,
    kDigitalSegmentA | kDigitalSegmentB | kDigitalSegmentC,
    kDigitalSegmentA | kDigitalSegmentB | kDigitalSegmentC |
        kDigitalSegmentD | kDigitalSegmentE | kDigitalSegmentF |
        kDigitalSegmentG,
    kDigitalSegmentA | kDigitalSegmentB | kDigitalSegmentC |
        kDigitalSegmentD | kDigitalSegmentF | kDigitalSegmentG,
};

uint32_t Elapsed(uint32_t now_ms, uint32_t since_ms) {
  return now_ms - since_ms;
}

bool Pending(uint32_t now_ms, uint32_t deadline_ms) {
  return static_cast<int32_t>(now_ms - deadline_ms) < 0;
}

float Smoothstep(float t) {
  if (t <= 0.0f)
    return 0.0f;
  if (t >= 1.0f)
    return 1.0f;
  return t * t * (3.0f - 2.0f * t);
}

float LightProfile(float t) {
  constexpr float kPeak = DigitalClockModel::kLightPeak;
  constexpr float kOver = DigitalClockModel::kLightOvershoot;
  if (t <= kPeak)
    return (1.0f + kOver) * Smoothstep(t / kPeak);
  return 1.0f + kOver *
                    (1.0f - Smoothstep((t - kPeak) / (1.0f - kPeak)));
}

}  // namespace

uint8_t DigitalSegmentsFor(int digit) {
  return digit >= 0 && digit <= 9 ? kDigitSegments[digit] : 0;
}

void DigitalClockDigits(int hour, int minute,
                        int digits[kDigitalDigitCount]) {
  const bool hour_valid = hour >= 0 && hour <= 23;
  const bool minute_valid = minute >= 0 && minute <= 59;
  digits[0] = hour_valid ? hour / 10 : -1;
  digits[1] = hour_valid ? hour % 10 : -1;
  digits[2] = minute_valid ? minute / 10 : -1;
  digits[3] = minute_valid ? minute % 10 : -1;
}

void DigitalClockModel::StartLoad(uint32_t now_ms) {
  this->Clear();
  this->colon_.target = kColonDimLevel;
  this->colon_.from = kColonDimLevel;
  this->colon_.level = kColonDimLevel;
  this->load_started_ms_ = now_ms;
  this->loading_ = true;
}

void DigitalClockModel::Clear() {
  for (auto &digit : this->segments_) {
    for (Segment &segment : digit)
      segment = Segment{};
  }
  this->colon_ = Colon{};
  this->loading_ = false;
}

void DigitalClockModel::Advance_(Segment &segment, uint32_t now_ms) {
  if (!segment.animating || Pending(now_ms, segment.start_ms))
    return;
  const uint32_t span = segment.lit ? kLightMs : kFadeMs;
  const uint32_t elapsed = Elapsed(now_ms, segment.start_ms);
  if (elapsed >= span) {
    segment.level = segment.lit ? 1.0f : 0.0f;
    segment.animating = false;
    return;
  }
  const float t = static_cast<float>(elapsed) / static_cast<float>(span);
  if (segment.lit) {
    segment.level =
        segment.from + (1.0f - segment.from) * LightProfile(t);
  } else {
    segment.level = segment.from * (1.0f - Smoothstep(t));
  }
}

void DigitalClockModel::AdvanceColon_(Colon &colon, uint32_t now_ms) {
  if (!colon.animating || Pending(now_ms, colon.start_ms))
    return;
  const uint32_t elapsed = Elapsed(now_ms, colon.start_ms);
  if (elapsed >= kColonFadeMs) {
    colon.level = colon.target;
    colon.animating = false;
    return;
  }
  const float t =
      static_cast<float>(elapsed) / static_cast<float>(kColonFadeMs);
  colon.level = colon.from + (colon.target - colon.from) * Smoothstep(t);
}

void DigitalClockModel::Retarget_(int digit, int segment, bool lit,
                                  uint32_t start_ms) {
  Segment &state = this->segments_[digit][segment];
  if (state.lit == lit)
    return;
  state.lit = lit;
  state.from = state.level;
  state.start_ms = start_ms;
  state.animating = true;
}

void DigitalClockModel::RetargetColon_(float target, uint32_t now_ms) {
  if (this->colon_.target == target)
    return;
  this->colon_.target = target;
  this->colon_.from = this->colon_.level;
  this->colon_.start_ms = now_ms;
  this->colon_.animating = true;
}

void DigitalClockModel::Update(int hour, int minute, int second,
                               uint32_t now_ms) {
  for (auto &digit : this->segments_) {
    for (Segment &segment : digit)
      Advance_(segment, now_ms);
  }
  AdvanceColon_(this->colon_, now_ms);

  int digits[kDigits];
  DigitalClockDigits(hour, minute, digits);
  if (digits[0] < 0 || digits[2] < 0 || second < 0 || second > 59) {
    this->Clear();
    return;
  }

  uint8_t masks[kDigits];
  for (int digit = 0; digit < kDigits; digit++)
    masks[digit] = DigitalSegmentsFor(digits[digit]);

  if (this->loading_) {
    this->loading_ = false;
    for (int digit = 0; digit < kDigits; digit++) {
      const uint32_t start =
          this->load_started_ms_ + digit * kLoadStaggerMs;
      for (int segment = 0; segment < kSegments; segment++) {
        this->Retarget_(digit, segment,
                        (masks[digit] & (1u << segment)) != 0, start);
      }
    }
  } else {
    int last_changed = -1;
    for (int digit = 0; digit < kDigits; digit++) {
      for (int segment = 0; segment < kSegments; segment++) {
        const bool lit = (masks[digit] & (1u << segment)) != 0;
        if (this->segments_[digit][segment].lit != lit)
          last_changed = digit;
      }
    }
    for (int digit = 0; digit <= last_changed; digit++) {
      const uint32_t start =
          now_ms + (last_changed - digit) * kRippleMs;
      for (int segment = 0; segment < kSegments; segment++) {
        this->Retarget_(digit, segment,
                        (masks[digit] & (1u << segment)) != 0, start);
      }
    }
  }

  this->RetargetColon_(second % 2 == 0 ? 1.0f : kColonDimLevel, now_ms);

  for (auto &digit : this->segments_) {
    for (Segment &segment : digit)
      Advance_(segment, now_ms);
  }
  AdvanceColon_(this->colon_, now_ms);
}

float DigitalClockModel::Level(int digit, int segment) const {
  if (digit < 0 || digit >= kDigits || segment < 0 || segment >= kSegments)
    return 0.0f;
  return this->segments_[digit][segment].level;
}

}  // namespace pixoo::clock
