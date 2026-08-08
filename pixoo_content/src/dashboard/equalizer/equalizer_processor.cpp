#include "equalizer_processor.h"

#include <algorithm>
#include <cstring>
#include <new>

namespace pixoo {

namespace {

// Calibrated per-band references for the panel microphone (~62 Hz - 6 kHz
// grouping), measured on-device. `kFloorSeed` is each band's quiet-room noise
// ceiling: below it the room is silent, so the bar reads zero. `kCeilSeed` is a
// loud-but-not-party level that should fill the bar. Levels normalize between
// the two per band, which both gates the noise and evens the bars so bass does
// not swamp the treble.
const float kFloorSeed[kBands] = {2100, 1650, 1150, 900, 650, 450, 350, 500,
                                  350,  360,  430,  320, 180, 280, 180, 240};
const float kCeilSeed[kBands] = {82000, 69000, 42000, 17000, 10000, 9500,
                                 5700,  6500,  6500,  6000,  2900,  4900,
                                 950,   800,   750,   600};

}  // namespace

EqualizerProcessor::EqualizerProcessor()
    : history_(new (std::nothrow) History{}) {}

void EqualizerProcessor::AddSamples(const int32_t *samples, size_t count) {
  this->AddSampleBytes(reinterpret_cast<const uint8_t *>(samples),
                       count * sizeof(*samples));
}

void EqualizerProcessor::AddSampleBytes(const uint8_t *bytes, size_t byte_count) {
  const size_t sample_count = byte_count / sizeof(int32_t);
  for (size_t i = 0; i < sample_count; i++) {
    int32_t sample;
    std::memcpy(&sample, bytes + i * sizeof(sample), sizeof(sample));
    // The in-progress window is the slot after the last published one.
    const uint32_t slot = this->written_.load(std::memory_order_relaxed) % kRing;
    this->window_[slot][this->fill_++] = static_cast<float>(sample >> 8);
    if (this->fill_ == kFftSize) {
      // Publish the completed window and start the next slot; filling never
      // stops, so capture is continuous across callbacks.
      this->fill_ = 0;
      this->written_.fetch_add(1, std::memory_order_release);
    }
  }
}

void EqualizerProcessor::seed_defaults_() {
  // Seed the percentile anchors from the on-device calibration so the mapping is
  // sensible before enough history has accumulated: the quiet-room floor is the
  // low anchor, the measured loud level the high anchor. They are replaced by
  // the real percentiles once the window fills.
  for (int b = 0; b < kBands; b++) {
    this->low_[b] = kFloorSeed[b];
    this->high_[b] = kCeilSeed[b];
  }
  this->seeded_ = true;
}

namespace {

// The value at fractional rank `pct` in `values` (size n), via a partial
// selection. n is small (<= kHistory) and this runs once per band per Poll on
// the main loop. `scratch` is caller-provided working space of at least n.
float Percentile(const float *values, int n, float pct, float *scratch) {
  std::memcpy(scratch, values, n * sizeof(float));
  int k = static_cast<int>(pct * (n - 1) + 0.5f);
  if (k < 0) k = 0;
  if (k >= n) k = n - 1;
  std::nth_element(scratch, scratch + k, scratch + n);
  return scratch[k];
}

}  // namespace

bool EqualizerProcessor::Poll(float out_levels[kBands]) {
  if (!this->history_) return false;
  const uint32_t written = this->written_.load(std::memory_order_acquire);
  if (written == this->read_)
    return false;  // no complete window since the last Poll

  // Consume every window published since the last Poll. If the producer lapped
  // the ring (Poll fell far behind), skip to the most recent kRing-1 windows so
  // the newest audio wins and the slot the producer is currently filling
  // (written % kRing) is never read.
  uint32_t first = this->read_;
  if (written - first > static_cast<uint32_t>(kRing - 1))
    first = written - (kRing - 1);

  SpectrumBands sb{};
  int count = 0;
  for (uint32_t w = first; w < written; w++) {
    const SpectrumBands one = AnalyzeWindow(this->window_[w % kRing]);
    for (int b = 0; b < kBands; b++) sb.magnitude[b] += one.magnitude[b];
    count++;
  }
  // Discard if the producer reached the oldest slot just read (it reuses that
  // slot at window first + kRing).
  if (this->written_.load(std::memory_order_acquire) - first >=
      static_cast<uint32_t>(kRing))
    return false;
  for (int b = 0; b < kBands; b++) sb.magnitude[b] /= count;
  this->read_ = written;

  if (!this->seeded_) this->seed_defaults_();

  // Append this window's magnitudes to each band's rolling history.
  for (int b = 0; b < kBands; b++)
    this->history_->values[b][this->hist_pos_] = sb.magnitude[b];
  this->hist_pos_ = (this->hist_pos_ + 1) % kHistory;
  if (this->hist_count_ < kHistory) this->hist_count_++;

  // Recompute each band's low/high percentiles from its history. A true
  // percentile discards the extreme tails, so a brief transient (a clap) sits
  // in the discarded top fraction and cannot move the scale; only sustained
  // level changes shift the percentiles, over the window length.
  //
  // The percentiles are only meaningful once the window holds a spread of
  // readings; below kWarmup they collapse (p10 == p90) and would pin every bar
  // to one height. Until then, and blending across the warmup, fall back to the
  // calibrated seed anchors so the display is sensible from the first frame.
  static constexpr int kWarmup = 64;
  float scratch[kHistory];
  for (int b = 0; b < kBands; b++) {
    const float p_low = Percentile(this->history_->values[b], this->hist_count_,
                                   kLowPct, scratch);
    const float p_high = Percentile(this->history_->values[b], this->hist_count_,
                                    kHighPct, scratch);
    if (this->hist_count_ >= kWarmup) {
      this->low_[b] = p_low;
      this->high_[b] = p_high;
    } else {
      // Blend seed -> percentile as the window fills.
      const float t = static_cast<float>(this->hist_count_) / kWarmup;
      this->low_[b] = kFloorSeed[b] * (1.0f - t) + p_low * t;
      this->high_[b] = kCeilSeed[b] * (1.0f - t) + p_high * t;
    }
    // The measured noise floor still anchors silence to an empty bar.
    if (this->low_[b] < kFloorSeed[b]) this->low_[b] = kFloorSeed[b];
    // If the recent signal has almost no spread (a sustained steady tone, or
    // silence), the percentiles collapse and cannot define a range. Fall back
    // to the calibrated span for this band so the bar still reflects absolute
    // level instead of pinning to a single height.
    const float min_span = (kCeilSeed[b] - kFloorSeed[b]) * 0.15f;
    if (this->high_[b] - this->low_[b] < min_span)
      this->high_[b] = this->low_[b] + min_span;
  }

  // Map each band: kLowPct percentile -> kLevelAtLow, kHighPct -> kLevelAtHigh,
  // linear in between and extrapolated past the ends, then clamped to [0, 1].
  // A magnitude at or below the calibrated noise floor reads as an empty bar, so
  // a quiet room stays dark regardless of the percentile anchors.
  for (int b = 0; b < kBands; b++) {
    if (sb.magnitude[b] <= kFloorSeed[b]) {
      out_levels[b] = 0.0f;
      continue;
    }
    const float slope =
        (kLevelAtHigh - kLevelAtLow) / (this->high_[b] - this->low_[b]);
    float level = kLevelAtLow + (sb.magnitude[b] - this->low_[b]) * slope;
    if (level < 0.0f) level = 0.0f;
    if (level > 1.0f) level = 1.0f;
    out_levels[b] = level;
  }
  return true;
}

}  // namespace pixoo
