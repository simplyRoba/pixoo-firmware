#pragma once

#include <cstdint>

#include "equalizer_face.h"
#include "esphome/components/display/display.h"

namespace esphome::pixoo64::dashboard::equalizer {

// Oscilloscope-style face over a warping aurora field. The 16 bands (bass left,
// treble right) set a deflection magnitude per column that rides a travelling
// carrier, so the smooth curve weaves above and below the centre line and
// flattens to the centre when silent. A white line leads, with cyan and magenta
// echo lines trailing behind it and a broad coloured halo underneath. The
// indigo-blue-violet-magenta background drifts over time and brightens with
// loudness.
class WaveformFace : public EqualizerFace {
 public:
  void OnShow(uint32_t now_ms) override;
  void Tick(const EqualizerView &view, uint32_t now_ms) override;
  void Render(display::Display &display,
              const EqualizerView &view) const override;

 protected:
  // Temporally smoothed per-band amplitudes so the drawn line glides instead of
  // snapping frame to frame. `disp_` drives the white line; `echo_fast_` and
  // `echo_slow_` are progressively laggier copies for the cyan and magenta
  // trailing echoes.
  float disp_[kBars] = {0};
  float echo_fast_[kBars] = {0};
  float echo_slow_[kBars] = {0};
  // Carrier phase advanced from the tick clock, so the weave and the background
  // plasma drift over time.
  float phase_ = {0};
  // Smoothed overall loudness in [0, 1], driving the background and halo.
  float energy_ = {0};
};

}  // namespace esphome::pixoo64::dashboard::equalizer
