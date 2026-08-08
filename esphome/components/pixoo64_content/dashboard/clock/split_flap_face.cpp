#include "split_flap_face.h"

#include <cmath>

#include "split_flap_digits.h"

namespace esphome::pixoo64::clockface {
namespace {

using pixoo::clock::FlapView;

const Color kBackground(0, 0, 0);
const Color kDigit(235, 238, 245);
const Color kHinge(0, 0, 0);
const Color kAxle(70, 72, 84);

// Card face, lit from above: the top row of a half is kCardTop, the bottom row
// kCardBottom, interpolated in between. A leaf is lifted out of the stack, so
// it is drawn a shade brighter, and the half a leaf hangs over is shadowed.
constexpr int kCardTop[3] = {30, 30, 36};
constexpr int kCardBottom[3] = {16, 16, 20};
constexpr float kLeafLift = 1.7f;
constexpr float kShadow = 0.45f;

// Two cards per row, hours above minutes.
constexpr int kCardW = 30;
constexpr int kCardH = 28;
constexpr int kCardX[2] = {1, 33};
constexpr int kCardY[2] = {2, 34};
// Hinge row: the leaf covers everything above it, the lower half everything
// below. The two halves are the same physical leaf seen from either face, so
// they must be equally tall.
constexpr int kSplitRow = kCardH / 2;
static_assert(kSplitRow * 2 == kCardH, "leaf halves must be equally tall");

// A font cannot be used: the drop squashes a digit to a fraction of its height,
// which needs per-row sampling of the glyph. Digit geometry and its coverage
// table live in split_flap_digits.h.
constexpr int kDigitX = (kCardW - kDigitW) / 2;
constexpr int kDigitY = (kCardH - kDigitH) / 2;

uint8_t DigitCoverage(int digit, int gx, int gy) {
  if (digit < 0 || digit > 9)
    return 0;
  return kDigitCoverage[digit][gy][gx];
}

// Blends digit ink over the card already drawn at this pixel.
Color Blend(Color card, uint8_t coverage) {
  const int a = coverage;
  const int inv = 255 - a;
  return Color(static_cast<uint8_t>((kDigit.r * a + card.r * inv) / 255),
               static_cast<uint8_t>((kDigit.g * a + card.g * inv) / 255),
               static_cast<uint8_t>((kDigit.b * a + card.b * inv) / 255));
}

// Inset of the card's own pixels on `row`, rounding the four corners off.
int CornerInset(int row) {
  if (row == 0 || row == kCardH - 1)
    return 2;
  if (row == 1 || row == kCardH - 2)
    return 1;
  return 0;
}

// Card fill for `row`, shaded within its own half and scaled by `tint`.
Color CardColor(int row, float tint) {
  const int half_row = row < kSplitRow ? row : row - kSplitRow;
  const float t = static_cast<float>(half_row) / (kSplitRow - 1);
  Color c;
  uint8_t *channel[3] = {&c.r, &c.g, &c.b};
  for (int i = 0; i < 3; i++) {
    const float v =
        (kCardTop[i] + (kCardBottom[i] - kCardTop[i]) * t) * tint;
    *channel[i] = static_cast<uint8_t>(v < 0.0f ? 0.0f : (v > 255.0f ? 255.0f : v));
  }
  return c;
}

void FillRows(display::Display &d, int x0, int y0, int row_begin, int row_end,
              float tint) {
  for (int row = row_begin; row < row_end; row++) {
    const int inset = CornerInset(row);
    d.horizontal_line(x0 + inset, y0 + row, kCardW - 2 * inset,
                      CardColor(row, tint));
  }
}

// Draws card row `dest_row` with the digit content of card row `src_row`,
// blended over the card shade `tint` that row was filled with.
void DrawDigitRow(display::Display &d, int x0, int y0, int digit, int dest_row,
                  int src_row, float tint) {
  const int gy = src_row - kDigitY;
  if (gy < 0 || gy >= kDigitH)
    return;
  const Color card = CardColor(dest_row, tint);
  for (int gx = 0; gx < kDigitW; gx++) {
    const uint8_t coverage = DigitCoverage(digit, gx, gy);
    if (coverage != 0)
      d.draw_pixel_at(x0 + kDigitX + gx, y0 + dest_row, Blend(card, coverage));
  }
}

void DrawHalf(display::Display &d, int x0, int y0, int digit, int row_begin,
              int row_end, float tint) {
  for (int row = row_begin; row < row_end; row++)
    DrawDigitRow(d, x0, y0, digit, row, row, tint);
}

// Squashes the [src_begin, src_begin + src_span) rows of `digit` into the card
// rows [row_begin, row_end).
void DrawLeaf(display::Display &d, int x0, int y0, int digit, int row_begin,
              int row_end, float src_begin, float src_span) {
  const int rows = row_end - row_begin;
  if (rows <= 0)
    return;
  FillRows(d, x0, y0, row_begin, row_end, kLeafLift);
  for (int row = row_begin; row < row_end; row++) {
    const float t = (static_cast<float>(row - row_begin) + 0.5f) /
                    static_cast<float>(rows);
    const int src_row = static_cast<int>(src_begin + t * src_span);
    DrawDigitRow(d, x0, y0, digit, row, src_row, kLeafLift);
  }
}

void DrawCard(display::Display &d, int x0, int y0, const FlapView &view,
              bool show_digits) {
  FillRows(d, x0, y0, 0, kCardH, 1.0f);
  if (show_digits) {
    // Behind the leaf the card already reads as the incoming digit on top and
    // the outgoing one below, which is what a real flap exposes mid-drop.
    DrawHalf(d, x0, y0, view.to, 0, kSplitRow, 1.0f);
    DrawHalf(d, x0, y0, view.from, kSplitRow, kCardH, 1.0f);

    if (view.flipping && view.phase < 0.5f) {
      // A falling flap accelerates under gravity and stops dead at the hinge.
      const float fall = view.phase * 2.0f;
      const float scale = 1.0f - fall * fall;
      const int rows = static_cast<int>(std::lround(kSplitRow * scale));
      FillRows(d, x0, y0, kSplitRow, kCardH, kShadow);
      DrawHalf(d, x0, y0, view.from, kSplitRow, kCardH, kShadow);
      // Front of the leaf, the outgoing digit's top, dropping onto the hinge.
      DrawLeaf(d, x0, y0, view.from, kSplitRow - rows, kSplitRow, 0.0f,
               static_cast<float>(kSplitRow));
    } else if (view.flipping) {
      // Back of the leaf, the incoming digit's bottom, swinging out of the
      // hinge and decelerating into the lower half.
      const float swing = view.phase * 2.0f - 1.0f;
      const float scale = 1.0f - (1.0f - swing) * (1.0f - swing);
      const int rows = static_cast<int>(std::lround(kSplitRow * scale));
      DrawLeaf(d, x0, y0, view.to, kSplitRow, kSplitRow + rows,
               static_cast<float>(kSplitRow), static_cast<float>(kSplitRow));
    }
  }
  d.horizontal_line(x0, y0 + kSplitRow - 1, kCardW, kHinge);
  // Axle pins the flaps pivot on.
  d.draw_pixel_at(x0, y0 + kSplitRow - 1, kAxle);
  d.draw_pixel_at(x0 + kCardW - 1, y0 + kSplitRow - 1, kAxle);
}

}  // namespace

void SplitFlapFace::Tick(const ClockTime &time, uint32_t now_ms) {
  this->has_time_ = time.valid;
  if (!time.valid)
    return;
  int digits[pixoo::clock::kFlapCount];
  pixoo::clock::FlapDigits(time.hour, time.minute, digits);
  if (this->spin_pending_) {
    // Settle on the current time first: the spin lands every column on the
    // digit it will show, which is only known once the clock is valid.
    this->spin_pending_ = false;
    this->model_.Update(digits, now_ms);
    this->model_.SpinAll(now_ms);
  }
  this->model_.Update(digits, now_ms);
}

void SplitFlapFace::OnShow(uint32_t now_ms) {
  (void) now_ms;
  this->spin_pending_ = true;
}

void SplitFlapFace::Render(display::Display &display) const {
  display.fill(kBackground);
  for (int i = 0; i < pixoo::clock::kFlapCount; i++) {
    DrawCard(display, kCardX[i % 2], kCardY[i / 2], this->model_.View(i),
             this->has_time_);
  }
}

}  // namespace esphome::pixoo64::clockface
