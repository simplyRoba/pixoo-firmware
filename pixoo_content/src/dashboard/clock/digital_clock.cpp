#include "digital_clock.h"

#include <cmath>

namespace pixoo::clock {
namespace {

float Smoothstep(float t) {
  if (t <= 0.0f)
    return 0.0f;
  if (t >= 1.0f)
    return 1.0f;
  return t * t * (3.0f - 2.0f * t);
}

float Progress(uint32_t now_ms, uint32_t start_ms, uint32_t duration_ms) {
  return Smoothstep(static_cast<float>(now_ms - start_ms) / duration_ms);
}

double Wrap(double phase) { return phase - std::floor(phase); }

constexpr uint32_t kSecondsEaseMs = 240;

}  // namespace

uint8_t DigitalSegmentsFor(int digit) {
  constexpr uint8_t kSegments[] = {
      0x3f, 0x06, 0x5b, 0x4f, 0x66, 0x6d, 0x7d, 0x07, 0x7f, 0x6f};
  return digit >= 0 && digit <= 9 ? kSegments[digit] : 0;
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
  this->loading_ = true;
  this->load_started_ms_ = now_ms;
}

void DigitalClockModel::Clear() { *this = DigitalClockModel{}; }

void DigitalClockModel::Advance_(uint32_t now_ms) {
  for (Glyph &glyph : this->digits_) {
    glyph.mix = glyph.from_mix + (1.0f - glyph.from_mix) *
        Progress(now_ms, glyph.start_ms, kDigitFadeMs);
  }
  this->live_phase_ = this->live_from_ +
      (this->live_target_ - this->live_from_) *
          Progress(now_ms, this->live_started_ms_, kSecondsEaseMs);
  if (this->loading_) {
    this->time_level_ = Progress(now_ms, this->load_started_ms_, kTimeFadeMs);
    this->intro_mix_ = Progress(now_ms, this->load_started_ms_, LoadMs());
    if (now_ms - this->load_started_ms_ >= LoadMs())
      this->loading_ = false;
  }
}

void DigitalClockModel::Update(int hour, int minute, int second,
                               uint32_t now_ms) {
  int digits[kDigits];
  DigitalClockDigits(hour, minute, digits);
  if (digits[0] < 0 || digits[2] < 0 || second < 0 || second > 59) {
    this->Clear();
    return;
  }

  if (!this->has_time_) {
    for (int i = 0; i < kDigits; ++i) {
      this->digits_[i].current = digits[i];
      this->digits_[i].previous = digits[i];
    }
    this->has_time_ = true;
    this->second_ = second;
    this->live_phase_ = this->live_from_ = this->live_target_ = second / 60.0;
    this->live_started_ms_ = now_ms;
    if (!this->loading_)
      this->time_level_ = 1.0f;
  }

  // Sample all ongoing movement before accepting a new civil-time target.
  this->Advance_(now_ms);
  for (int i = 0; i < kDigits; ++i) {
    Glyph &glyph = this->digits_[i];
    if (glyph.current == digits[i])
      continue;
    if (glyph.previous == digits[i]) {
      const int old_current = glyph.current;
      glyph.current = glyph.previous;
      glyph.previous = old_current;
      glyph.from_mix = 1.0f - glyph.mix;
    } else {
      // Two glyph slots retain the dominant visible glyph on a third target.
      glyph.previous = glyph.mix >= 0.5f ? glyph.current : glyph.previous;
      glyph.current = digits[i];
      glyph.from_mix = 0.0f;
    }
    glyph.mix = glyph.from_mix;
    glyph.start_ms = now_ms;
  }
  if (second != this->second_) {
    this->second_ = second;
    this->live_from_ = this->live_phase_;
    const double target = second / 60.0;
    const double delta = Wrap(target - this->live_phase_ + 0.5) - 0.5;
    this->live_target_ = this->live_phase_ + delta;
    this->live_started_ms_ = now_ms;
  }
}

int DigitalClockModel::Digit(int index) const {
  return index >= 0 && index < kDigits ? this->digits_[index].current : -1;
}

int DigitalClockModel::PreviousDigit(int index) const {
  return index >= 0 && index < kDigits ? this->digits_[index].previous : -1;
}

float DigitalClockModel::DigitMix(int index) const {
  return index >= 0 && index < kDigits ? this->digits_[index].mix : 0.0f;
}

float DigitalClockModel::PerimeterPhase() const {
  const float phase = static_cast<float>(
      Wrap(this->live_phase_ - (1.0 - this->intro_mix_)));
  return phase < 1.0f ? phase : 0.0f;
}

float DigitalClockModel::TrackLevel(float phase) const {
  constexpr float kBaseline = 0.35f;
  if (!this->has_time_ || !std::isfinite(phase))
    return kBaseline;
  constexpr float kHeadWidth = 0.012f;
  constexpr float kFadeLength = 1.0f - kHeadWidth;
  const float behind = static_cast<float>(Wrap(this->PerimeterPhase() - phase));
  const float wave = behind <= kFadeLength
                         ? 1.0f - behind / kFadeLength
                         : 1.0f - Smoothstep((1.0f - behind) / kHeadWidth);
  return kBaseline + (1.0f - kBaseline) * wave;
}

}  // namespace pixoo::clock
