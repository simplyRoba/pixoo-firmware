#pragma once

#include <cstdint>

namespace pixoo::clock {

constexpr int kDigitalDigitCount = 4;

// Shared decimal-segment encoding used by the timing dashboard.
constexpr uint8_t kDigitalSegmentA = 1u << 0;
constexpr uint8_t kDigitalSegmentB = 1u << 1;
constexpr uint8_t kDigitalSegmentC = 1u << 2;
constexpr uint8_t kDigitalSegmentD = 1u << 3;
constexpr uint8_t kDigitalSegmentE = 1u << 4;
constexpr uint8_t kDigitalSegmentF = 1u << 5;
constexpr uint8_t kDigitalSegmentG = 1u << 6;
uint8_t DigitalSegmentsFor(int digit);

// Decimal digits of HH:MM. Out-of-range fields yield -1 for their pair.
void DigitalClockDigits(int hour, int minute,
                        int digits[kDigitalDigitCount]);

// Glyph crossfades and a clockwise seconds wake, independent of drawing.
class DigitalClockModel {
 public:
  static constexpr int kDigits = kDigitalDigitCount;
  static constexpr uint32_t kTimeFadeMs = 700;
  static constexpr uint32_t kDigitFadeMs = 180;
  static constexpr uint32_t LoadMs() { return 1600; }

  void StartLoad(uint32_t now_ms);
  void Clear();
  void Update(int hour, int minute, int second, uint32_t now_ms);

  bool HasTime() const { return this->has_time_; }
  float TimeLevel() const { return this->time_level_; }
  int Digit(int index) const;
  int PreviousDigit(int index) const;
  float DigitMix(int index) const;
  float PerimeterPhase() const;
  float TrackLevel(float phase) const;

 private:
  struct Glyph {
    int current{-1};
    int previous{-1};
    float mix{1.0f};
    float from_mix{1.0f};
    uint32_t start_ms{0};
  };

  void Advance_(uint32_t now_ms);

  Glyph digits_[kDigits];
  bool has_time_{false};
  bool loading_{false};
  uint32_t load_started_ms_{0};
  float time_level_{0.0f};
  float intro_mix_{1.0f};
  int second_{-1};
  // Unwrapped turns preserve direction through the civil 59 -> 0 boundary.
  double live_phase_{0.0};
  double live_from_{0.0};
  double live_target_{0.0};
  uint32_t live_started_ms_{0};
};

}  // namespace pixoo::clock
