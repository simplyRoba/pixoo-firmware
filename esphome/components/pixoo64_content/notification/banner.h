#pragma once

#include "esphome/components/display/display.h"
#include "esphome/components/pixoo64_content/blend_canvas.h"

namespace esphome::pixoo64::content::banner {

constexpr int kHeight = 12;
constexpr int kFadeHeight = 12;

// The final fade sample has zero alpha and needs no backing pixels.
constexpr int BackdropHeight(int height) { return height + kFadeHeight - 1; }

inline void DrawBackdrop(display::Display &display, int height, Color accent,
                         bool translucent) {
  const Color black(0, 0, 0);
  BlendCanvas *canvas = BlendCanvasOf(display);
  if (translucent && canvas != nullptr) {
    for (int y = 1; y < BackdropHeight(height); ++y) {
      float alpha = 0.74f;
      if (y >= height) {
        const float t = static_cast<float>(y - height + 1) / kFadeHeight;
        const float smooth = t * t * (3.0f - 2.0f * t);
        alpha *= 1.0f - smooth;
      }
      if (alpha <= 0.0f)
        continue;
      canvas->BlendSpan(0, y, 64, black, alpha);
    }
    display.horizontal_line(0, 0, 64, accent);
  } else {
    display.filled_rectangle(0, 0, 64, height, black);
    display.rectangle(0, 0, 64, height, accent);
  }
}

}  // namespace esphome::pixoo64::content::banner
