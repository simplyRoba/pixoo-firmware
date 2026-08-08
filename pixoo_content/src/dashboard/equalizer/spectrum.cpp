#include "spectrum.h"

#include <cmath>
#include <utility>

namespace pixoo {

namespace {

// Band bin edges: edges[b]..edges[b+1] is the (half-open) bin range for band b.
// Log-spaced from bin 1 (skip DC) to kBandTopBin, but each edge is forced
// strictly increasing so every band gets at least one bin -- a pure log curve
// is flat at the low end and would leave the lowest bands with zero bins (dead
// bars).
const int *BandEdges() {
  static int edges[kBands + 1];
  static bool built = false;
  if (!built) {
    edges[0] = 1;
    for (int b = 1; b <= kBands; b++) {
      const float t = static_cast<float>(b) / kBands;
      int e = static_cast<int>(std::pow(static_cast<float>(kBandTopBin), t) +
                               0.5f);
      if (e <= edges[b - 1]) e = edges[b - 1] + 1;
      edges[b] = e;
    }
    edges[kBands] = kBandTopBin;
    built = true;
  }
  return edges;
}

}  // namespace

void ApplyHann(float *samples, int n) {
  const float step = 2.0f * static_cast<float>(M_PI) / (n - 1);
  for (int i = 0; i < n; i++) {
    samples[i] *= 0.5f - 0.5f * std::cos(step * i);
  }
}

void Fft(float *data, int n) {
  for (int i = 1, j = 0; i < n; i++) {
    int bit = n >> 1;
    for (; j & bit; bit >>= 1) j ^= bit;
    j ^= bit;
    if (i < j) {
      std::swap(data[2 * i], data[2 * j]);
      std::swap(data[2 * i + 1], data[2 * j + 1]);
    }
  }
  for (int len = 2; len <= n; len <<= 1) {
    const float ang = -2.0f * static_cast<float>(M_PI) / len;
    const float wr = std::cos(ang), wi = std::sin(ang);
    for (int i = 0; i < n; i += len) {
      float cr = 1.0f, ci = 0.0f;
      for (int k = 0; k < len / 2; k++) {
        const int a = 2 * (i + k);
        const int b = 2 * (i + k + len / 2);
        const float br = data[b] * cr - data[b + 1] * ci;
        const float bi = data[b] * ci + data[b + 1] * cr;
        data[b] = data[a] - br;
        data[b + 1] = data[a + 1] - bi;
        data[a] += br;
        data[a + 1] += bi;
        const float ncr = cr * wr - ci * wi;
        ci = cr * wi + ci * wr;
        cr = ncr;
      }
    }
  }
}

int BandForBin(int bin) {
  if (bin <= 0) return 0;
  const int *edges = BandEdges();
  for (int b = 0; b < kBands; b++) {
    if (bin >= edges[b] && bin < edges[b + 1]) return b;
  }
  return kBands - 1;
}

SpectrumBands AnalyzeWindow(const float *samples) {
  SpectrumBands out{};

  // Copy, remove DC offset (mean), apply Hann window. Static work buffers keep
  // ~6 KB off the caller's stack (a mic-task callback stack is too small);
  // single-threaded use only (one caller on the main loop).
  static float buf[kFftSize];
  double sum = 0;
  for (int i = 0; i < kFftSize; i++) sum += samples[i];
  const float mean = static_cast<float>(sum / kFftSize);
  for (int i = 0; i < kFftSize; i++) buf[i] = samples[i] - mean;
  ApplyHann(buf, kFftSize);

  static float cx[2 * kFftSize];
  for (int i = 0; i < kFftSize; i++) {
    cx[2 * i] = buf[i];
    cx[2 * i + 1] = 0.0f;
  }
  Fft(cx, kFftSize);

  // Peak magnitude per band, scaled so a pure tone of sample amplitude A reads
  // ~A in its band (Hann coherent gain 0.5 over N/2 one-sided bins => 4/N).
  const int *edges = BandEdges();
  const float norm = 4.0f / kFftSize;
  for (int b = 0; b < kBands; b++) {
    float peak = 0.0f;
    for (int bin = edges[b]; bin < edges[b + 1]; bin++) {
      const float re = cx[2 * bin];
      const float im = cx[2 * bin + 1];
      const float mag = std::sqrt(re * re + im * im);
      if (mag > peak) peak = mag;
    }
    out.magnitude[b] = peak * norm;
  }
  out.valid = true;
  return out;
}

}  // namespace pixoo
