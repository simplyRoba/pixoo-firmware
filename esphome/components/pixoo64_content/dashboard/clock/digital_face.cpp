#include "digital_face.h"

#include <cmath>

#include "esphome/components/pixoo64_content/aa_draw.h"

namespace esphome::pixoo64::clockface {
namespace {

namespace draw = content;

const Color kBackground(0, 0, 0);
const Color kInk(240, 240, 235);
constexpr float kPi = 3.14159265358979323846f;
constexpr float kDigitX[] = {10.0f, 21.0f, 36.0f, 47.0f};
constexpr float kDigitY = 20.0f;
constexpr float kStrokeRadius = 1.35f;
constexpr float kMarkRadius = .38f;

// Original condensed, round-ended numeral centerlines, in a 7 x 24 cell.
// Negative x lifts the pen between the two strokes of the four.
struct Point { float x, y; };
constexpr Point kZero[] = {
    {3.5f, 0}, {1.8f, .4f}, {.6f, 1.8f}, {0, 4}, {0, 20},
    {.6f, 22.2f}, {1.8f, 23.6f}, {3.5f, 24}, {5.2f, 23.6f},
    {6.4f, 22.2f}, {7, 20}, {7, 4}, {6.4f, 1.8f}, {5.2f, .4f}, {3.5f, 0}};
constexpr Point kOne[] = {{.5f, 3}, {4, 0}, {4, 24}};
constexpr Point kTwo[] = {
    {0, 3}, {.7f, 1.2f}, {2, .2f}, {4, 0}, {6, 1}, {7, 3}, {7, 6},
    {6.3f, 8.5f}, {4.8f, 11.5f}, {2, 16}, {.5f, 19}, {0, 21}, {0, 24}, {7, 24}};
constexpr Point kThree[] = {
    {0, 2}, {1.5f, .4f}, {3.5f, 0}, {5.5f, .6f}, {6.7f, 2}, {7, 4},
    {7, 7.5f}, {6.2f, 10}, {4.5f, 11.5f}, {2.5f, 12},
    {4.5f, 12.5f}, {6.2f, 14}, {7, 16.5f}, {7, 20}, {6.7f, 22},
    {5.5f, 23.4f}, {3.5f, 24}, {1.5f, 23.6f}, {0, 22}};
constexpr Point kFour[] = {
    {5, 0}, {0, 16}, {7, 16}, {-1, 0}, {6, 7}, {6, 24}};
constexpr Point kFive[] = {
    {7, 0}, {0, 0}, {0, 11}, {3.5f, 10.5f}, {5.5f, 11.2f}, {6.7f, 13},
    {7, 15}, {7, 20}, {6.5f, 22}, {5.2f, 23.5f}, {3, 24}, {1.2f, 23.5f}, {0, 22}};
constexpr Point kSix[] = {
    {6.5f, 1}, {4.5f, 0}, {2.5f, .5f}, {1, 2.5f}, {0, 6}, {0, 19.5f},
    {.5f, 22}, {1.8f, 23.6f}, {3.5f, 24}, {5.2f, 23.6f}, {6.5f, 22},
    {7, 19.5f}, {7, 16}, {6.5f, 13.5f}, {5.2f, 12}, {3.5f, 11.5f}, {1.5f, 12}, {0, 14}};
constexpr Point kSeven[] = {{0, 0}, {7, 0}, {7, 3}, {2, 24}};
constexpr Point kEight[] = {
    {3.5f, 11.5f}, {1.5f, 10.5f}, {.3f, 8}, {0, 4}, {.5f, 1.8f},
    {1.8f, .4f}, {3.5f, 0}, {5.2f, .4f}, {6.5f, 1.8f}, {7, 4},
    {6.7f, 8}, {5.5f, 10.5f}, {3.5f, 11.5f}, {1.3f, 13.5f}, {0, 16},
    {0, 20}, {.5f, 22}, {1.8f, 23.6f}, {3.5f, 24}, {5.2f, 23.6f},
    {6.5f, 22}, {7, 20}, {7, 16}, {5.7f, 13.5f}, {3.5f, 11.5f}};
constexpr Point kNine[] = {
    {0, 23}, {2.5f, 24}, {4.5f, 23.5f}, {6, 21.5f}, {7, 18}, {7, 4.5f},
    {6.5f, 2}, {5.2f, .4f}, {3.5f, 0}, {1.8f, .4f}, {.5f, 2},
    {0, 4.5f}, {0, 8}, {.5f, 10.5f}, {1.8f, 12}, {3.5f, 12.5f}, {5.5f, 12}, {7, 10}};

struct Glyph { const Point *points; int count; };
template<size_t N> constexpr Glyph GlyphOf(const Point (&points)[N]) {
  return {points, static_cast<int>(N)};
}
constexpr Glyph kGlyphs[] = {
    GlyphOf(kZero), GlyphOf(kOne), GlyphOf(kTwo), GlyphOf(kThree), GlyphOf(kFour),
    GlyphOf(kFive), GlyphOf(kSix), GlyphOf(kSeven), GlyphOf(kEight), GlyphOf(kNine)};

float GlyphCoverage(int digit, float x, float y) {
  if (digit < 0 || digit > 9)
    return 0.0f;
  const Glyph &glyph = kGlyphs[digit];
  float coverage = 0.0f;
  for (int i = 1; i < glyph.count; ++i) {
    const Point &a = glyph.points[i - 1];
    const Point &b = glyph.points[i];
    if (a.x < 0 || b.x < 0)
      continue;
    const float c = draw::CapsuleCoverage(x, y, a.x, a.y, b.x, b.y,
                                          kStrokeRadius, kStrokeRadius);
    if (c > coverage)
      coverage = c;
  }
  return coverage;
}

// Seconds sample equal arc-length positions clockwise from top center.
// Straight marks align to pixel centers for a uniform one-pixel width; the
// normal keeps each mark perpendicular to the rounded-square track.
struct TrackPoint { float x, y, nx, ny; };
TrackPoint OnTrack(float phase) {
  constexpr float radius = 10.0f;
  constexpr float straight = 37.0f;
  constexpr float arc = kPi * radius * .5f;
  constexpr float side = straight + arc;
  float distance = phase * (4.0f * side) + straight * .5f;
  if (distance >= 4.0f * side)
    distance -= 4.0f * side;
  const int quadrant = static_cast<int>(distance / side);
  distance -= quadrant * side;
  TrackPoint point;
  if (distance < straight) {
    point = {std::floor(13.5f + distance) + .5f, 3.5f, 0.0f, -1.0f};
  } else {
    const float angle = -kPi * .5f + (distance - straight) / radius;
    point.nx = std::cos(angle);
    point.ny = std::sin(angle);
    point.x = 50.5f + radius * point.nx;
    point.y = 13.5f + radius * point.ny;
  }
  for (int i = 0; i < quadrant; ++i) {
    const float x = point.x;
    point.x = 64.0f - point.y;
    point.y = x;
    const float nx = point.nx;
    point.nx = -point.ny;
    point.ny = nx;
  }
  return point;
}

// Match the brightest pixel of a diagonal mark to a pixel-aligned edge mark.
// Coverage remains proportional, preserving the corner's anti-aliased shape.
float MarkGain(const TrackPoint &p) {
  if (p.nx == 0.0f || p.ny == 0.0f)
    return 1.0f;
  const int cx = static_cast<int>(std::floor(p.x));
  const int cy = static_cast<int>(std::floor(p.y));
  float peak = 0.0f;
  for (int y = cy - 2; y <= cy + 2; ++y) {
    for (int x = cx - 2; x <= cx + 2; ++x) {
      const float coverage = draw::CapsuleCoverage(
          x + .5f, y + .5f, p.x - p.nx, p.y - p.ny,
          p.x + p.nx, p.y + p.ny, kMarkRadius, kMarkRadius);
      if (coverage > peak)
        peak = coverage;
    }
  }
  return peak > 0.0f ? (kMarkRadius + .5f) / peak : 1.0f;
}

constexpr int kGlyphWidth = 11;
constexpr int kGlyphHeight = 28;
constexpr int kMarkSize = 5;

uint8_t CoverageByte(float coverage) {
  return static_cast<uint8_t>(draw::Clamp01(coverage) * 255.0f + .5f);
}

struct MarkRaster {
  int8_t x, y;
  uint8_t coverage[kMarkSize][kMarkSize];
};

// Shared retained coverage is built during face construction, before animation
// starts. Rendering only reads masks and blends pixels; geometry is fixed.
struct DigitalRaster {
  uint8_t glyphs[10][kGlyphHeight][kGlyphWidth];
  MarkRaster marks[60];
  uint8_t colon[4][4];

  DigitalRaster() {
    for (int digit = 0; digit < 10; ++digit) {
      for (int y = 0; y < kGlyphHeight; ++y) {
        for (int x = 0; x < kGlyphWidth; ++x)
          this->glyphs[digit][y][x] = CoverageByte(
              GlyphCoverage(digit, x - 1.5f, y - 1.5f));
      }
    }
    for (int tick = 0; tick < 60; ++tick) {
      const TrackPoint p = OnTrack(tick / 60.0f);
      MarkRaster &mark = this->marks[tick];
      mark.x = static_cast<int8_t>(std::floor(p.x)) - 2;
      mark.y = static_cast<int8_t>(std::floor(p.y)) - 2;
      const float gain = MarkGain(p);
      for (int y = 0; y < kMarkSize; ++y) {
        for (int x = 0; x < kMarkSize; ++x) {
          mark.coverage[y][x] = CoverageByte(draw::CapsuleCoverage(
              mark.x + x + .5f, mark.y + y + .5f,
              p.x - p.nx, p.y - p.ny, p.x + p.nx, p.y + p.ny,
              kMarkRadius, kMarkRadius) * gain);
        }
      }
    }
    for (int y = 0; y < 4; ++y) {
      for (int x = 0; x < 4; ++x)
        this->colon[y][x] = CoverageByte(
            draw::DiscCoverage(x - 1.5f, y - 1.5f, {0, 0, 1.55f}));
    }
  }
};

const DigitalRaster &Raster() {
  static const DigitalRaster raster;
  return raster;
}

float RasterGlyph(const DigitalRaster &raster, int digit, int x, int y) {
  return digit >= 0 && digit < 10 ? raster.glyphs[digit][y][x] / 255.0f : 0.0f;
}

}  // namespace

DigitalFace::DigitalFace() { (void) Raster(); }

void DigitalFace::Tick(const ClockTime &time, uint32_t now_ms) {
  if (!time.valid) {
    this->model_.Clear();
    this->load_pending_ = true;
    return;
  }
  if (this->load_pending_) {
    this->load_pending_ = false;
    this->model_.StartLoad(now_ms);
  }
  this->model_.Update(time.hour, time.minute, time.second, now_ms);
}

void DigitalFace::OnShow(uint32_t now_ms) {
  (void) now_ms;
  this->load_pending_ = true;
}

void DigitalFace::Render(display::Display &display) const {
  display.fill(kBackground);
  auto *canvas = draw::BlendCanvasOf(display);
  const DigitalRaster &raster = Raster();
  for (int tick = 0; tick < 60; ++tick) {
    const MarkRaster &mark = raster.marks[tick];
    const float level = this->model_.TrackLevel(tick / 60.0f) / 255.0f;
    for (int y = 0; y < kMarkSize; ++y) {
      for (int x = 0; x < kMarkSize; ++x) {
        const uint8_t coverage = mark.coverage[y][x];
        if (coverage != 0)
          draw::BlendAt(display, canvas, mark.x + x, mark.y + y, kInk,
                        coverage * level);
      }
    }
  }

  const float time_level = this->model_.TimeLevel();
  if (time_level <= 0.0f)
    return;
  for (int digit = 0; digit < pixoo::clock::DigitalClockModel::kDigits; ++digit) {
    const int current = this->model_.Digit(digit);
    const int previous = this->model_.PreviousDigit(digit);
    const float mix = this->model_.DigitMix(digit);
    const int x0 = static_cast<int>(kDigitX[digit]) - 2;
    const int y0 = static_cast<int>(kDigitY) - 2;
    for (int y = 0; y < kGlyphHeight; ++y) {
      for (int x = 0; x < kGlyphWidth; ++x) {
        const float ink = RasterGlyph(raster, current, x, y);
        const float old_ink = previous == current || mix >= 1.0f
                                  ? ink : RasterGlyph(raster, previous, x, y);
        draw::BlendAt(display, canvas, x0 + x, y0 + y, kInk,
                      (old_ink + (ink - old_ink) * mix) * time_level);
      }
    }
  }
  for (int y = 0; y < 4; ++y) {
    for (int x = 0; x < 4; ++x) {
      const float alpha = raster.colon[y][x] / 255.0f * time_level;
      draw::BlendAt(display, canvas, 30 + x, 25 + y, kInk, alpha);
      draw::BlendAt(display, canvas, 30 + x, 35 + y, kInk, alpha);
    }
  }
}

}  // namespace esphome::pixoo64::clockface
