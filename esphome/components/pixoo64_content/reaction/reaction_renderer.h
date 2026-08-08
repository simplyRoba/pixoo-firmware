#pragma once

#include <cstdint>

#include "app_state.h"
#include "esphome/components/display/display.h"

namespace esphome::pixoo64::content {

class ReactionRenderer {
 public:
  void Render(display::Display &display, pixoo::Reaction reaction,
              uint32_t visible_elapsed_ms) const;
};

}  // namespace esphome::pixoo64::content
