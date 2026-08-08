#pragma once

#include <algorithm>
#include <cstdint>

#include "bars_face.h"
#include "dashboard/dashboard.h"
#include "equalizer_face.h"
#include "equalizer_processor.h"
#include "esphome/components/display/display.h"
#include "waveform_face.h"

namespace esphome::pixoo64::dashboard {

using equalizer::EqualizerFace;
using equalizer::EqualizerView;

// Sound-reactive spectrum dashboard. It receives normalized levels from the
// microphone adapter, eases each bar toward them and holds a peak, then hands
// the resulting view to one face, which owns the drawing. The face is bound per
// dashboard instance at composition time, so each face is its own selectable
// dashboard. SetBands is also the render-test entry point.
class EqualizerDashboard : public Dashboard, public pixoo::EqualizerLevelsSink {
 public:
  void set_face(EqualizerFace *face) { this->face_ = face; }

  void SetLevels(const float levels[pixoo::kBands]) override {
    this->SetBands(levels, pixoo::kBands);
  }

  // Updates bars from kBars levels in [0, 1] (also the render-test entry point).
  void SetBands(const float *levels, int count) {
    const int n = std::min(count, equalizer::kBars);
    for (int b = 0; b < n; b++) {
      float v = levels[b];
      if (v < 0.0f) v = 0.0f;
      if (v > 1.0f) v = 1.0f;
      // Ease toward the new value both ways, quicker up than down, so a peak is
      // climbed over a few frames rather than jumped to.
      const float rate = v >= level_[b] ? kAttack : kRelease;
      level_[b] += (v - level_[b]) * rate;
      // Peak-hold: jump up, drift down.
      if (level_[b] >= peak_[b])
        peak_[b] = level_[b];
      else
        peak_[b] -= kPeakFall;
      if (peak_[b] < 0.0f) peak_[b] = 0.0f;
    }
  }

  bool available() const override { return this->face_ != nullptr; }
  bool requires_microphone() const override { return true; }
  pixoo::EqualizerLevelsSink *levels_sink() override { return this; }

  void OnShow(uint32_t now_ms) override {
    if (this->face_ != nullptr)
      this->face_->OnShow(now_ms);
  }

  void Tick(uint32_t now_ms) override {
    if (this->face_ != nullptr)
      this->face_->Tick(this->view_(), now_ms);
  }

  void Render(display::Display &display) const override {
    this->face_->Render(display, this->view_());
  }

 protected:
  static constexpr float kAttack = 0.45f;    // per-update rise toward a peak
  static constexpr float kRelease = 0.28f;   // per-update fall toward quiet
  static constexpr float kPeakFall = 0.02f;  // per-update peak drop

  EqualizerView view_() const {
    EqualizerView view{};
    for (int b = 0; b < equalizer::kBars; b++) {
      view.level[b] = level_[b];
      view.peak[b] = peak_[b];
    }
    return view;
  }

  float level_[equalizer::kBars] = {0};
  float peak_[equalizer::kBars] = {0};
  EqualizerFace *face_{nullptr};
};

class BarsEqualizerDashboard : public EqualizerDashboard {
 public:
  BarsEqualizerDashboard() { this->set_face(&this->face_impl_); }

 protected:
  equalizer::BarsFace face_impl_;
};

class WaveformEqualizerDashboard : public EqualizerDashboard {
 public:
  WaveformEqualizerDashboard() { this->set_face(&this->face_impl_); }

 protected:
  equalizer::WaveformFace face_impl_;
};

}  // namespace esphome::pixoo64::dashboard
