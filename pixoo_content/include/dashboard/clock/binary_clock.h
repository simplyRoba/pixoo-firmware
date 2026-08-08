#pragma once

#include <cstdint>

namespace pixoo {

// Binary-coded-decimal clock movement for an HH:MM:SS display: how lit each dot
// is over time, no layout and no pixels.
namespace clock {

class BinaryClockModel {
 public:
  // One column per decimal digit of HH:MM:SS; four rows per column carrying the
  // bit values 8, 4, 2, 1 from the top down.
  static constexpr int kColumns = 6;
  static constexpr int kRows = 4;

  // A dot lighting up is driven past its resting level by kPopOvershoot,
  // peaking kPopPeak of the way through kPopMs and settling onto rest by the
  // end of it.
  static constexpr uint32_t kPopMs = 130;
  static constexpr float kPopOvershoot = 0.25f;
  static constexpr float kPopPeak = 0.30f;

  // A dot going out fades down to the unlit level over kGlowMs rather than
  // snapping off.
  static constexpr uint32_t kGlowMs = 200;

  // Dots that change together do so a beat apart, right to left from the
  // rightmost column that changed, so a carry is seen travelling into the
  // higher digits.
  static constexpr uint32_t kRippleMs = 25;

  // A load sweep runs the columns in left to right this far apart, each
  // column's dots popping on as the sweep reaches it.
  static constexpr uint32_t kLoadStaggerMs = 70;

  // Bit value of row `row`: 8 at the top down to 1 at the bottom. Rows outside
  // 0..kRows-1 yield 0.
  static int RowBit(int row);
  // Decimal digits of HH:MM:SS, one per column. Fields outside their unit are
  // taken modulo it, negatives included.
  static void Digits(int hour, int minute, int second, int digits[kColumns]);
  // From the sweep starting to its last dot settling.
  static uint32_t LoadMs();

  // Sweeps every column in from unlit onto the time the next Update() reports.
  void StartLoad(uint32_t now_ms);
  // Puts every dot out at once and holds it there. A movement with no time to
  // show sits here, motionless.
  void Clear();
  void Update(int hour, int minute, int second, uint32_t now_ms);

  // 0 at the unlit level, 1 at rest when lit, and above 1 during the pop. A
  // bit a column's digit can never set stays at 0.
  float Level(int column, int row) const;

 private:
  struct Dot {
    bool lit{false};
    // Level the running transition started from, and the level now.
    float from{0.0f};
    float level{0.0f};
    // May be in the future while a ripple or sweep beat is pending.
    uint32_t start_ms{0};
    bool animating{false};
  };

  void Retarget_(int column, int row, bool lit, uint32_t start_ms);
  static void Advance_(Dot &dot, uint32_t now_ms);

  Dot dots_[kColumns][kRows];
  uint32_t load_started_ms_{0};
  bool loading_{false};
};

}  // namespace clock
}  // namespace pixoo
