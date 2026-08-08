#include "blend_canvas.h"

#include <cstddef>

namespace esphome::pixoo64::content {
namespace {

struct ActiveBinding {
  display::Display *display;
  BlendCanvas *canvas;
};

constexpr size_t kMaxBindingDepth = 4u;
ActiveBinding g_bindings[kMaxBindingDepth]{};
size_t g_binding_depth = 0u;
size_t g_overflow_depth = 0u;

}  // namespace

void PushActiveBlendCanvas(display::Display &display, BlendCanvas &canvas) {
  if (g_binding_depth >= kMaxBindingDepth) {
    ++g_overflow_depth;
    return;
  }
  g_bindings[g_binding_depth++] = {&display, &canvas};
}

void PopActiveBlendCanvas() {
  if (g_overflow_depth != 0u) {
    --g_overflow_depth;
  } else if (g_binding_depth != 0u) {
    --g_binding_depth;
  }
}

BlendCanvas *BlendCanvasOf(display::Display &display) {
  if (g_overflow_depth != 0u || g_binding_depth == 0u ||
      g_bindings[g_binding_depth - 1u].display != &display)
    return nullptr;
  return g_bindings[g_binding_depth - 1u].canvas;
}

}  // namespace esphome::pixoo64::content
