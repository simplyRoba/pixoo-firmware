#pragma once

#include <cstdint>

namespace pixoo::clock {

constexpr int kDigitalDigitCount = 4;
constexpr int kDigitalSegmentCount = 7;

constexpr uint8_t kDigitalSegmentA = 1u << 0;
constexpr uint8_t kDigitalSegmentB = 1u << 1;
constexpr uint8_t kDigitalSegmentC = 1u << 2;
constexpr uint8_t kDigitalSegmentD = 1u << 3;
constexpr uint8_t kDigitalSegmentE = 1u << 4;
constexpr uint8_t kDigitalSegmentF = 1u << 5;
constexpr uint8_t kDigitalSegmentG = 1u << 6;

// Seven-segment mask for a decimal digit. Values outside 0..9 yield no
// segments.
uint8_t DigitalSegmentsFor(int digit);

// Decimal digits of HH:MM. Out-of-range fields yield -1 for their pair.
void DigitalClockDigits(int hour, int minute,
                        int digits[kDigitalDigitCount]);

// Segment transitions for a 24-hour HH:MM clock. Lit segments flare gently as
// they arrive, extinguished segments fade away, and carries ripple from right
// to left. The colon breathes between two visible levels once per second.
class DigitalClockModel {
 public:
  static constexpr int kDigits = kDigitalDigitCount;
  static constexpr int kSegments = kDigitalSegmentCount;

  static constexpr uint32_t kLightMs = 180;
  static constexpr uint32_t kFadeMs = 220;
  static constexpr float kLightOvershoot = 0.20f;
  static constexpr float kLightPeak = 0.35f;
  static constexpr uint32_t kRippleMs = 40;
  static constexpr uint32_t kLoadStaggerMs = 75;
  static constexpr uint32_t kColonFadeMs = 300;
  static constexpr float kColonDimLevel = 0.32f;

  static constexpr uint32_t LoadMs() {
    constexpr uint32_t segment_load =
        (kDigits - 1) * kLoadStaggerMs + kLightMs;
    return segment_load > kColonFadeMs ? segment_load : kColonFadeMs;
  }

  // Clears the display, then sweeps the next reported time in left to right.
  void StartLoad(uint32_t now_ms);
  // Puts every segment and the colon out immediately.
  void Clear();
  void Update(int hour, int minute, int second, uint32_t now_ms);

  // 0 is off, 1 is the settled lit level, and values above 1 are the arrival
  // flare. Invalid indices yield 0.
  float Level(int digit, int segment) const;
  float ColonLevel() const { return this->colon_.level; }

 private:
  struct Segment {
    bool lit{false};
    float from{0.0f};
    float level{0.0f};
    uint32_t start_ms{0};
    bool animating{false};
  };

  struct Colon {
    float target{0.0f};
    float from{0.0f};
    float level{0.0f};
    uint32_t start_ms{0};
    bool animating{false};
  };

  static void Advance_(Segment &segment, uint32_t now_ms);
  static void AdvanceColon_(Colon &colon, uint32_t now_ms);
  void Retarget_(int digit, int segment, bool lit, uint32_t start_ms);
  void RetargetColon_(float target, uint32_t now_ms);

  Segment segments_[kDigits][kSegments];
  Colon colon_;
  uint32_t load_started_ms_{0};
  bool loading_{false};
};

}  // namespace pixoo::clock
