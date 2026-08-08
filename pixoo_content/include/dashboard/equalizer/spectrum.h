#pragma once

#include <cstddef>
#include <cstdint>

namespace pixoo {

// Fixed analysis parameters. kFftSize is a power of two; the mic feeds real
// samples, so the usable spectrum is kFftSize/2 magnitude bins.
constexpr int kFftSize = 512;
constexpr int kBins = kFftSize / 2;  // 256 magnitude bins
constexpr int kBands = 16;           // equalizer bars

// Highest bin folded into the bands. At 32 kHz over kFftSize samples each bin
// is 62.5 Hz, so bin 96 is ~6 kHz. The panel microphone produces no usable
// signal above ~6 kHz (only fixed hiss), so the bands stop there and all 16
// bars cover live spectrum (~62 Hz - 6 kHz) instead of wasting the top bars on
// noise.
constexpr int kBandTopBin = 96;

// Result of analyzing one window: kBands raw per-band magnitudes in
// sample-amplitude units (a pure tone of sample amplitude A reads ~A in its
// band). The processor maps these to 0..1 bar heights.
struct SpectrumBands {
  float magnitude[kBands];
  bool valid;
};

// Analyzes kFftSize real samples (any scale; DC offset removed and a Hann
// window applied), computes the magnitude spectrum, and folds the bins into
// kBands log-spaced bands (peak per band). Returns raw magnitudes; mapping to a
// bar height is a separate, tunable step.
SpectrumBands AnalyzeWindow(const float *samples);

// --- lower-level helpers (exposed for host tests) ---

// Maps a magnitude-bin index [0, kBins) to its band [0, kBands) using a
// log-frequency split, so bass and treble each get comparable bar width.
int BandForBin(int bin);

// Applies a Hann window in place: w[i] = 0.5 - 0.5*cos(2*pi*i/(N-1)).
void ApplyHann(float *samples, int n);

// In-place radix-2 complex FFT on interleaved {re, im}. `data` holds n complex
// pairs (2*n floats); transforms in place. n must be a power of two.
void Fft(float *data, int n);

}  // namespace pixoo
