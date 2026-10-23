#pragma once

#include <cstdint>
#include <limits>

#include "esphome/components/display/display.h"
#include "esphome/core/color.h"

namespace esphome::pixoo64::content {

// A drawing surface whose pixels can be read back, so an effect can composite
// translucently over whatever has already been drawn.
//
// ESPHome's display::Display is write-only, and the panel itself cannot be read
// back, but the frame is assembled in a RAM framebuffer that can. A medium such
// as fog needs the scene behind it to attenuate rather than be replaced, which
// an opaque write cannot express and covering a fraction of pixels only
// approximates as visible grain.
class BlendCanvas {
 public:
  virtual ~BlendCanvas() = default;

  // Mix `color` over the pixel already at (x, y). alpha 0 leaves it untouched,
  // alpha 1 replaces it. Out-of-range coordinates are ignored.
  virtual void BlendPixel(int x, int y, Color color, float alpha) = 0;

  // Replace a horizontal span with opaque RGB grayscale, including black.
  // The source contains at least length bytes; null and empty spans are ignored.
  virtual void WriteGrayscaleSpan(int x, int y, int length,
                                  const uint8_t *gray) {
    if (gray == nullptr || length <= 0)
      return;
    const int64_t end = static_cast<int64_t>(x) + length;
    for (int64_t px = x; px < end && px <= std::numeric_limits<int>::max(); ++px) {
      const uint8_t value = *gray++;
      this->BlendPixel(static_cast<int>(px), y, Color(value, value, value), 1.0f);
    }
  }

  // Composite a solid horizontal span with the same semantics as BlendPixel.
  // Non-positive lengths are empty; coordinates beyond int range are ignored.
  virtual void BlendSpan(int x, int y, int length, Color color, float alpha) {
    const int64_t end = static_cast<int64_t>(x) + length;
    for (int64_t px = x; px < end && px <= std::numeric_limits<int>::max(); ++px)
      this->BlendPixel(static_cast<int>(px), y, color, alpha);
  }
};

// The renderer publishes its blend surface for the duration of a frame.
// Renderers are handed a display::Display, which cannot express compositing,
// and the device build has no RTTI to recover the concrete type by cast.
// Bindings are display-scoped and stacked, so a nested render cannot composite
// into the wrong framebuffer. Rendering is synchronous and single-threaded.
void PushActiveBlendCanvas(display::Display &display, BlendCanvas &canvas);
void PopActiveBlendCanvas();
BlendCanvas *BlendCanvasOf(display::Display &display);

}  // namespace esphome::pixoo64::content
