#include "binary_face.h"

#include <cmath>

#include "pixoo_cmd.h"

namespace esphome::pixoo64::clockface {
namespace {

const Color kBackground(0, 0, 0);
const Color kUnlit(26, 28, 33);
const Color kHour(255, 176, 0);
const Color kMinute(235, 238, 245);
const Color kSecond(226, 44, 38);

// Resting dot radius, and the four row centers for bit values 8, 4, 2, 1 (top
// to bottom). Rows are one dot pitch apart and vertically centered on the
// panel.
constexpr float kRadius = 3.0f;
constexpr float kRowY[4] = {18.0f, 27.0f, 36.0f, 45.0f};

// How far past its resting look a dot is driven at the peak of its pop: extra
// radius in pixels per unit of level above 1, and how far its color is carried
// toward white over the same range.
constexpr float kPopRadiusGain = 1.4f;
constexpr float kPopWhitenGain = 0.6f;

// One column per decimal digit of HH:MM:SS. `x` is the dot center; the tighter
// pitch within a pair and the wider gap between pairs (hour|minute|second)
// group the six columns into three. `max_value` is the largest digit the column
// can hold, so a column carries only the placeholder dots it can ever light.
struct Column {
  float x;
  int max_value;
  const Color *color;
};

const Column kColumns[6] = {
    {5.0f, 2, &kHour},   {14.0f, 9, &kHour},
    {27.0f, 5, &kMinute}, {36.0f, 9, &kMinute},
    {49.0f, 5, &kSecond}, {58.0f, 9, &kSecond},
};

int ClampInt(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

uint8_t MixChannel(uint8_t from, uint8_t to, float t) {
  const float v = from + (to - from) * t;
  return static_cast<uint8_t>(v < 0.0f ? 0.0f : (v > 255.0f ? 255.0f : v));
}

Color Mix(Color from, Color to, float t) {
  return Color(MixChannel(from.r, to.r, t), MixChannel(from.g, to.g, t),
               MixChannel(from.b, to.b, t));
}

Color Over(Color under, Color over, float coverage) {
  const int a = static_cast<int>(coverage * 255.0f + 0.5f);
  const int inv = 255 - a;
  return Color(static_cast<uint8_t>((over.r * a + under.r * inv) / 255),
               static_cast<uint8_t>((over.g * a + under.g * inv) / 255),
               static_cast<uint8_t>((over.b * a + under.b * inv) / 255));
}

// Anti-aliased filled disc, blended over whatever is already in the buffer, so
// the round edge reads as a curve rather than a staircase.
void DrawDisc(display::Display &display, float cx, float cy, float radius,
              Color color) {
  const int x0 = ClampInt(static_cast<int>(std::floor(cx - radius - 1.0f)), 0,
                          pixoo::kWidth - 1);
  const int x1 = ClampInt(static_cast<int>(std::ceil(cx + radius + 1.0f)), 0,
                          pixoo::kWidth - 1);
  const int y0 = ClampInt(static_cast<int>(std::floor(cy - radius - 1.0f)), 0,
                          pixoo::kHeight - 1);
  const int y1 = ClampInt(static_cast<int>(std::ceil(cy + radius + 1.0f)), 0,
                          pixoo::kHeight - 1);
  for (int y = y0; y <= y1; y++) {
    for (int x = x0; x <= x1; x++) {
      const float dx = x + 0.5f - cx;
      const float dy = y + 0.5f - cy;
      const float d = std::sqrt(dx * dx + dy * dy);
      const float c = radius - d + 0.5f;
      const float coverage = c < 0.0f ? 0.0f : (c > 1.0f ? 1.0f : c);
      if (coverage > 0.0f)
        display.draw_pixel_at(x, y, Over(kBackground, color, coverage));
    }
  }
}

}  // namespace

void BinaryFace::Tick(const ClockTime &time, uint32_t now_ms) {
  if (!time.valid) {
    // The dots wait unlit, where the load sweep starts once the time arrives.
    this->model_.Clear();
    return;
  }
  if (this->load_pending_) {
    // The sweep runs onto the current time, which is only known once the clock
    // is valid.
    this->load_pending_ = false;
    this->model_.StartLoad(now_ms);
  }
  this->model_.Update(time.hour, time.minute, time.second, now_ms);
}

void BinaryFace::OnShow(uint32_t now_ms) {
  (void) now_ms;
  this->load_pending_ = true;
}

void BinaryFace::Render(display::Display &display) const {
  display.fill(kBackground);

  for (int c = 0; c < 6; c++) {
    const Column &col = kColumns[c];
    for (int r = 0; r < 4; r++) {
      if (pixoo::clock::BinaryClockModel::RowBit(r) > col.max_value)
        continue;  // digit can never reach this bit
      const float level = this->model_.Level(c, r);
      const float settled = level < 1.0f ? level : 1.0f;
      const float excess = level > 1.0f ? level - 1.0f : 0.0f;
      const Color color =
          Mix(Mix(kUnlit, *col.color, settled), Color(255, 255, 255),
              excess * kPopWhitenGain);
      DrawDisc(display, col.x, kRowY[r], kRadius + excess * kPopRadiusGain,
               color);
    }
  }
}

}  // namespace esphome::pixoo64::clockface
