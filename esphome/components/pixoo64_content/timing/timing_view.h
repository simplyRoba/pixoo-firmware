#pragma once

#include "app_state.h"
#include "esphome/components/display/display.h"
#include "esphome/components/font/font.h"

namespace esphome::pixoo64::content {

class TimingView {
 public:
  void Render(display::Display &display, const pixoo::TimingSnapshot &timing,
              font::Font *label_font) const;
};

}  // namespace esphome::pixoo64::content
