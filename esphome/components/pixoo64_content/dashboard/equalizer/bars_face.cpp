#include "bars_face.h"

#include "pixoo_framebuffer.h"

namespace esphome::pixoo64::dashboard::equalizer {

namespace {

constexpr int kWidth = pixoo::kWidth;    // 64
constexpr int kHeight = pixoo::kHeight;  // 64
constexpr int kBarWidth = kWidth / kBars;  // 4 px per bar, fills 64

// Height-based colour: green (low) -> amber (mid) -> red (top).
Color ColorForHeight(int y_from_bottom) {
  const float t = static_cast<float>(y_from_bottom) / (kHeight - 1);
  if (t < 0.5f) {
    // green -> amber
    const float k = t / 0.5f;
    return Color(static_cast<uint8_t>(k * 255), 255, 0);
  }
  // amber -> red
  const float k = (t - 0.5f) / 0.5f;
  return Color(255, static_cast<uint8_t>((1.0f - k) * 255), 0);
}

int LevelToPixels(float level) {
  if (level < 0.0f) level = 0.0f;
  if (level > 1.0f) level = 1.0f;
  return static_cast<int>(level * kHeight + 0.5f);
}

}  // namespace

void BarsFace::Render(display::Display &display,
                      const EqualizerView &view) const {
  display.fill(Color(0, 0, 0));
  for (int b = 0; b < kBars; b++) {
    const int x0 = b * kBarWidth;
    const int h = LevelToPixels(view.level[b]);
    // Bar body: columns x0..x0+kBarWidth-2 (1 px gap between bars).
    for (int yy = 0; yy < h; yy++) {
      const int y = kHeight - 1 - yy;
      const Color c = ColorForHeight(yy);
      for (int x = x0; x < x0 + kBarWidth - 1; x++) {
        display.draw_pixel_at(x, y, c);
      }
    }
    // Peak-hold marker: one white row at the peak height.
    const int ph = LevelToPixels(view.peak[b]);
    if (ph > 0) {
      const int y = kHeight - 1 - (ph >= kHeight ? kHeight - 1 : ph);
      for (int x = x0; x < x0 + kBarWidth - 1; x++) {
        display.draw_pixel_at(x, y, Color(255, 255, 255));
      }
    }
  }
}

}  // namespace esphome::pixoo64::dashboard::equalizer
