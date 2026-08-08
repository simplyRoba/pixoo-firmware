#pragma once

#include "equalizer_face.h"
#include "esphome/components/display/display.h"

namespace esphome::pixoo64::dashboard::equalizer {

// Draws kBars vertical bars across the 64x64 panel: height = level, colour by
// height (green low, amber mid, red top), with a white peak-hold marker per bar.
class BarsFace : public EqualizerFace {
 public:
  void Render(display::Display &display,
              const EqualizerView &view) const override;
};

}  // namespace esphome::pixoo64::dashboard::equalizer
