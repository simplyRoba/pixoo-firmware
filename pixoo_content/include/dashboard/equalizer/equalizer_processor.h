#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>

#include "spectrum.h"

namespace pixoo {

// Receives one normalized level for each equalizer band. This port keeps the
// DSP producer independent from the ESPHome-backed dashboard renderer.
class EqualizerLevelsSink {
 public:
  virtual ~EqualizerLevelsSink() = default;
  virtual void SetLevels(const float levels[kBands]) = 0;
};

// Turns raw microphone samples into equalizer band levels. AddSamples and
// AddSampleBytes fill analysis windows from the mic callback (small stack, no
// heavy work); Poll runs the FFT and maps magnitudes to display levels on the
// main loop.
//
// Capture is continuous: the mic callback fills consecutive windows into a small
// ring and never idles, so there is no dead time between the fixed-cadence
// Polls. Each Poll analyzes every window captured since the previous Poll and
// averages their spectra, so all audio contributes and the per-band magnitude
// feeding the mapping is steadier than a single short snapshot.
//
// Level mapping is per band and derived from the recent distribution of each
// band's loudness over a rolling window of about 12 seconds. Each band maps its
// window's 10th percentile to a low bar and its 90th percentile to a high bar,
// so quiet passages sit low, loud passages sit high, and the bar uses its whole
// range in between -- evened across the spectrum, at any volume, with no user
// setting. A real percentile ignores the extreme tails, so a single transient
// (a clap, a door slam) cannot move the scale; only sustained loudness changes
// re-scale, over the window length. The calibrated per-band noise floor still
// anchors silence to an empty bar. Framework-independent and host-tested.
class EqualizerProcessor {
 public:
  EqualizerProcessor();
  bool ready() const { return this->history_ != nullptr; }

  // Copies `count` 32-bit I2S sample words into the window (24-bit sample in the
  // high bits). Ignored while a completed window awaits Poll.
  void AddSamples(const int32_t *samples, size_t count);

  // Decodes complete 32-bit I2S sample words from bytes into aligned local
  // values before filling the window. Trailing incomplete bytes are ignored.
  void AddSampleBytes(const uint8_t *bytes, size_t byte_count);

  // If a window is ready, analyzes it, writes kBands levels in [0, 1] to
  // out_levels, and returns true; otherwise returns false.
  bool Poll(float out_levels[kBands]);

 private:
  // Rolling window of recent per-band magnitudes for the percentile mapping.
  // One reading is added per Poll (~33 ms), so kHistory readings span about 12
  // seconds -- long enough that a brief transient is a negligible fraction and
  // is discarded by the percentile, short enough to re-scale within seconds of
  // a real volume change.
  static constexpr int kHistory = 384;
  // Percentiles read from the window and the bar levels they map to: the 10th
  // percentile sits low and the 90th sits high, stretching normal dynamics
  // across the bar.
  static constexpr float kLowPct = 0.10f;
  static constexpr float kHighPct = 0.90f;
  static constexpr float kLevelAtLow = 0.20f;
  static constexpr float kLevelAtHigh = 0.80f;

  void seed_defaults_();

  // Ring of analysis windows. The mic callback (producer) fills the window at
  // write_ % kRing, and on completion publishes it by advancing written_; it
  // never blocks or idles. Poll (consumer) analyzes every window from read_ up
  // to the published written_ count, so no captured audio is dropped and no
  // fixed gap sits between Polls. If the producer laps the consumer (Poll fell
  // behind by more than the ring), the consumer skips to the most recent whole
  // ring, keeping the newest audio. One producer, one consumer.
  static constexpr int kRing = 4;
  float window_[kRing][kFftSize];
  int fill_ = 0;                       // producer-only: samples in current window
  std::atomic<uint32_t> written_{0};   // published completed-window count
  uint32_t read_ = 0;                  // consumer-only: consumed-window count

  bool seeded_ = false;
  // Rolling history of per-band magnitudes and the mapping derived from it each
  // Poll: low_[b] is the kLowPct percentile, high_[b] the kHighPct percentile.
  // hist_count_ grows to kHistory then the ring overwrites oldest-first. The
  // bulk history is heap-backed so the target's allocator can place it in PSRAM.
  struct History {
    float values[kBands][kHistory]{};
  };
  std::unique_ptr<History> history_;
  int hist_pos_ = 0;
  int hist_count_ = 0;
  float low_[kBands];
  float high_[kBands];
};

}  // namespace pixoo
