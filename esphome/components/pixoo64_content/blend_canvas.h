#pragma once

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
