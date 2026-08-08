#include "waveform_face.h"

#include <cmath>
#include <memory>
#include <new>

#ifdef ESP_PLATFORM
#include "esp_heap_caps.h"
#endif

#include "pixoo_cmd.h"

namespace esphome::pixoo64::dashboard::equalizer {
namespace {

constexpr int kWidth = pixoo::kWidth;    // 64
constexpr int kHeight = pixoo::kHeight;  // 64
constexpr float kPi = 3.14159265358979323846f;

// The trace is a spatial carrier wave whose amplitude at each column is the
// band level there, so bass (left) and treble (right) swell their own stretch
// of the line while the whole thing weaves smoothly with no hard corners.
constexpr float kCenter = 32.0f;      // dial centre, on the row boundary
constexpr float kMaxDeflect = 24.0f;  // peak swing at full level
constexpr float kWavelength = 40.0f;  // carrier wavelength in pixels
constexpr float kHalf = 1.05f;        // line half-thickness
// Centre prominence: the deflection is scaled by a smooth raised-cosine that is
// full at the screen centre and tapers to kEdgeWeight at the left/right edges,
// so the middle of the wave reads taller than the ends.
constexpr float kEdgeWeight = 0.35f;

// Motion and echo lag.
constexpr float kPhaseSpeed = 0.004f;    // rad per ms
constexpr float kDispAlpha = 0.28f;      // per-frame follow of the drawn line
constexpr float kEchoFastAlpha = 0.4f;   // cyan amplitude follow
constexpr float kEchoSlowAlpha = 0.18f;  // magenta amplitude follow
constexpr float kEchoFastPhase = 0.5f;   // cyan spatial trail (rad)
constexpr float kEchoSlowPhase = 1.1f;   // magenta spatial trail (rad)

const Color kWhite(255, 255, 255);
const Color kCyan(0, 170, 255);
const Color kMagenta(190, 0, 255);

// Background plasma: a drifting indigo-blue-violet-magenta field whose brightness
// scales between a visible quiet floor and a loud ceiling with `energy_`.
constexpr float kBgDrift = 0.0016f;    // plasma drift rate vs. phase
constexpr float kBgFloor = 0.34f;      // brightness at silence
constexpr float kBgCeil = 1.0f;        // brightness at full loudness
constexpr float kEnergyAlpha = 0.25f;  // loudness follow rate
constexpr float kBgBase = 0.35f;       // illumination in the plasma troughs

const Color kBgIndigo(18, 4, 55);
const Color kBgBlue(0, 65, 150);
const Color kBgViolet(105, 10, 180);
const Color kBgMagenta(180, 0, 180);

// A broad coloured halo follows the leading trace below the sharp echo lines.
constexpr float kHaloRadius = 9.0f;
constexpr float kHaloFloor = 0.14f;
constexpr float kHaloCeil = 0.50f;
const Color kHaloBlue(0, 95, 220);
const Color kHaloViolet(145, 20, 235);
const Color kHaloMagenta(235, 20, 235);

// Control points sit at the centre of each 4-px band column: 2, 6, ..., 62,
// laid out straight across the width -- bass on the left, treble on the right,
// like the bars face.
float BandCenterX(int b) {
  return (b + 0.5f) * static_cast<float>(kWidth) / kBars;
}

// Cosine interpolation of the band values into one value per column, so the
// amplitude envelope has no corners at the control points.
void InterpBands(const float *bands, float *out) {
  for (int x = 0; x < kWidth; x++) {
    const float fx = x + 0.5f;
    if (fx <= BandCenterX(0)) {
      out[x] = bands[0];
      continue;
    }
    if (fx >= BandCenterX(kBars - 1)) {
      out[x] = bands[kBars - 1];
      continue;
    }
    int b = 0;
    while (b < kBars - 2 && fx > BandCenterX(b + 1)) b++;
    const float x0 = BandCenterX(b);
    const float t = (fx - x0) / (BandCenterX(b + 1) - x0);
    const float tt = 0.5f - 0.5f * std::cos(kPi * t);
    out[x] = bands[b] + (bands[b + 1] - bands[b]) * tt;
  }
}

float Clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

// Fast sine via a power-of-two lookup table. The plasma and carrier motion do
// not need more precision, and this keeps the per-pixel background off the
// transcendental path so a whole frame stays within the tick budget.
constexpr int kSinBits = 9;
constexpr int kSinN = 1 << kSinBits;  // 512
float g_sin_lut[kSinN];
bool g_sin_ready = false;
void EnsureSinLut() {
  if (g_sin_ready) return;
  for (int i = 0; i < kSinN; i++)
    g_sin_lut[i] = std::sin(2.0f * kPi * i / kSinN);
  g_sin_ready = true;
}
inline float FastSin(float radians) {
  const float scaled = radians * (kSinN / (2.0f * kPi));
  // Bias by a large multiple of kSinN so the truncation is a floor and the
  // index is non-negative before the power-of-two mask wraps it.
  const int i = (static_cast<int>(scaled) + kSinN * 1024) & (kSinN - 1);
  return g_sin_lut[i];
}

struct RadiusDeleter {
  void operator()(float *storage) const {
#ifdef ESP_PLATFORM
    heap_caps_free(storage);
#else
    delete[] storage;
#endif
  }
};

// Distance-from-centre table for the radial plasma ripple, filled once. It is
// application-lifetime bulk data and requests PSRAM, with an internal fallback.
std::unique_ptr<float[], RadiusDeleter> g_radius;
bool g_radius_allocation_attempted = false;
void EnsureRadius() {
  if (g_radius || g_radius_allocation_attempted) return;
  g_radius_allocation_attempted = true;
#ifdef ESP_PLATFORM
  auto *storage = static_cast<float *>(heap_caps_malloc(
      sizeof(float) * kWidth * kHeight, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (storage == nullptr)
    storage = static_cast<float *>(heap_caps_malloc(
        sizeof(float) * kWidth * kHeight,
        MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
#else
  auto *storage = new (std::nothrow) float[kWidth * kHeight];
#endif
  if (storage == nullptr) return;
  for (int y = 0; y < kHeight; y++)
    for (int x = 0; x < kWidth; x++) {
      const float dx = x + 0.5f - kCenter;
      const float dy = y + 0.5f - kCenter;
      storage[y * kWidth + x] = std::sqrt(dx * dx + dy * dy);
    }
  g_radius.reset(storage);
}

// A trace as per-column geometry: the line row and a vertical half-thickness
// that keeps the on-screen line ~kHalf wide whatever the slope, so per-pixel
// coverage is a vertical distance test with no sqrt.
struct Trace {
  float y[kWidth];
  float vhalf[kWidth];
};

// Raised-cosine centre weight in [kEdgeWeight, 1]: 1 at the screen centre,
// kEdgeWeight at the left/right edges.
float CentreWeight(int x) {
  const float d = std::fabs(x + 0.5f - kCenter) / kCenter;  // 0 centre, 1 edge
  const float bump = 0.5f + 0.5f * FastSin(kPi * (0.5f - d));  // 1 at d=0, 0 at d=1
  return kEdgeWeight + (1.0f - kEdgeWeight) * bump;
}

void BuildTrace(const float *amp, float phase, Trace *tr) {
  for (int x = 0; x < kWidth; x++) {
    const float carrier = FastSin(2.0f * kPi * (x + 0.5f) / kWavelength + phase);
    tr->y[x] = kCenter + amp[x] * kMaxDeflect * CentreWeight(x) * carrier;
  }
  for (int x = 0; x < kWidth; x++) {
    const int xl = x > 0 ? x - 1 : 0;
    const int xr = x < kWidth - 1 ? x + 1 : kWidth - 1;
    const float slope = (tr->y[xr] - tr->y[xl]) / (xr - xl);
    tr->vhalf[x] = kHalf * std::sqrt(1.0f + slope * slope) + 0.5f;
  }
}

inline float TraceCoverage(const Trace &tr, int x, float py) {
  return Clamp01(tr.vhalf[x] - std::fabs(py - tr.y[x]));
}

Color LerpColor(Color from, Color to, float amount) {
  const float t = Clamp01(amount);
  return Color(static_cast<uint8_t>(from.r + (to.r - from.r) * t + 0.5f),
               static_cast<uint8_t>(from.g + (to.g - from.g) * t + 0.5f),
               static_cast<uint8_t>(from.b + (to.b - from.b) * t + 0.5f));
}

Color PaletteColor(float position) {
  const float p = Clamp01(position);
  if (p < 1.0f / 3.0f)
    return LerpColor(kBgIndigo, kBgBlue, p * 3.0f);
  if (p < 2.0f / 3.0f)
    return LerpColor(kBgBlue, kBgViolet, (p - 1.0f / 3.0f) * 3.0f);
  return LerpColor(kBgViolet, kBgMagenta,
                   (p - 2.0f / 3.0f) * 3.0f);
}

Color HaloColor(float position) {
  const float p = Clamp01(position);
  return p < 0.5f ? LerpColor(kHaloBlue, kHaloViolet, p * 2.0f)
                  : LerpColor(kHaloViolet, kHaloMagenta,
                              (p - 0.5f) * 2.0f);
}

Color ScaleColor(Color color, float amount) {
  const float scale = Clamp01(amount);
  return Color(static_cast<uint8_t>(color.r * scale + 0.5f),
               static_cast<uint8_t>(color.g * scale + 0.5f),
               static_cast<uint8_t>(color.b * scale + 0.5f));
}

Color Over(Color under, Color over, float coverage) {
  const int a = static_cast<int>(coverage * 255.0f + 0.5f);
  const int inv = 255 - a;
  return Color(static_cast<uint8_t>((over.r * a + under.r * inv) / 255),
               static_cast<uint8_t>((over.g * a + under.g * inv) / 255),
               static_cast<uint8_t>((over.b * a + under.b * inv) / 255));
}

}  // namespace

void WaveformFace::OnShow(uint32_t now_ms) {
  (void) now_ms;
  // The line and its echoes wind out from the flat centre line each time the
  // face is shown.
  for (int b = 0; b < kBars; b++) {
    this->disp_[b] = 0.0f;
    this->echo_fast_[b] = 0.0f;
    this->echo_slow_[b] = 0.0f;
  }
  this->energy_ = 0.0f;
}

void WaveformFace::Tick(const EqualizerView &view, uint32_t now_ms) {
  this->phase_ = now_ms * kPhaseSpeed;
  float sum = 0.0f;
  for (int b = 0; b < kBars; b++) {
    this->disp_[b] += (view.level[b] - this->disp_[b]) * kDispAlpha;
    this->echo_fast_[b] +=
        (this->disp_[b] - this->echo_fast_[b]) * kEchoFastAlpha;
    this->echo_slow_[b] +=
        (this->echo_fast_[b] - this->echo_slow_[b]) * kEchoSlowAlpha;
    sum += view.level[b];
  }
  const float loudness = sum / kBars;
  this->energy_ += (loudness - this->energy_) * kEnergyAlpha;
}

void WaveformFace::Render(display::Display &display,
                          const EqualizerView &view) const {
  (void) view;  // The drawn line follows the temporally smoothed disp_/echoes.
  EnsureSinLut();
  EnsureRadius();

  float amp_white[kWidth];
  float amp_fast[kWidth];
  float amp_slow[kWidth];
  InterpBands(this->disp_, amp_white);
  InterpBands(this->echo_fast_, amp_fast);
  InterpBands(this->echo_slow_, amp_slow);

  Trace white, fast, slow;
  BuildTrace(amp_white, this->phase_, &white);
  BuildTrace(amp_fast, this->phase_ - kEchoFastPhase, &fast);
  BuildTrace(amp_slow, this->phase_ - kEchoSlowPhase, &slow);

  const float energy = Clamp01(this->energy_);
  const float brightness = kBgFloor + (kBgCeil - kBgFloor) * energy;
  const float halo_strength =
      kHaloFloor + (kHaloCeil - kHaloFloor) * energy;
  const float bg_t = this->phase_ * (kBgDrift / kPhaseSpeed);

  // The plasma is x*a + y*b + (x+y)*c + radial*d summed as sines. The first
  // three terms are separable, so precompute their per-row and per-column
  // sine contributions once instead of per pixel.
  float col_a[kWidth];
  float col_c[kWidth];
  for (int x = 0; x < kWidth; x++) {
    const float px = x + 0.5f;
    col_a[x] = FastSin(px * 0.11f + bg_t);
    col_c[x] = px * 0.08f + bg_t * 0.5f;  // phase; sined with row term below
  }

  for (int y = 0; y < kHeight; y++) {
    const float py = y + 0.5f;
    const float row_b = FastSin(py * 0.13f - bg_t * 0.8f);
    const float row_c = py * 0.08f;
    for (int x = 0; x < kWidth; x++) {
      // Warping violet field: four drifting sine ripples, then loudness gain.
      float radius;
      if (g_radius) {
        radius = g_radius[y * kWidth + x];
      } else {
        const float dx = x + 0.5f - kCenter;
        const float dy = y + 0.5f - kCenter;
        radius = std::sqrt(dx * dx + dy * dy);
      }
      const float diagonal = FastSin(col_c[x] + row_c);
      const float radial = FastSin(radius * 0.18f - bg_t * 1.3f);
      const float v = col_a[x] + row_b + diagonal + radial;
      const float field = Clamp01(0.5f + 0.125f * v);
      const float palette_position =
          Clamp01(0.5f + 0.20f * (col_a[x] - row_b) + 0.10f * radial);
      const float illumination =
          brightness * (kBgBase + (1.0f - kBgBase) * field);
      Color pixel = ScaleColor(PaletteColor(palette_position), illumination);

      float halo = Clamp01(1.0f - std::fabs(py - white.y[x]) / kHaloRadius);
      if (halo > 0.0f) {
        halo *= halo;
        const Color halo_color = HaloColor(palette_position);
        pixel = Over(pixel, halo_color, halo * halo_strength);
      }

      // Back to front: waveform halo, magenta slow echo, cyan fast echo, white
      // line on top.
      const float cs = TraceCoverage(slow, x, py);
      if (cs > 0.0f) pixel = Over(pixel, kMagenta, cs);
      const float cf = TraceCoverage(fast, x, py);
      if (cf > 0.0f) pixel = Over(pixel, kCyan, cf);
      const float cw = TraceCoverage(white, x, py);
      if (cw > 0.0f) pixel = Over(pixel, kWhite, cw);

      display.draw_pixel_at(x, y, pixel);
    }
  }
}

}  // namespace esphome::pixoo64::dashboard::equalizer
