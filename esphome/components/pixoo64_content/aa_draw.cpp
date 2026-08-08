#include "aa_draw.h"

#include <cmath>

#include "pixoo_cmd.h"

namespace esphome::pixoo64::content {
namespace {

int ClampInt(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

// Pixel range a shape can touch, clipped to the panel. `margin` covers the one
// pixel the anti-aliased edge reaches past the geometry.
struct Bounds {
  int x0, y0, x1, y1;
  bool empty() const { return this->x1 < this->x0 || this->y1 < this->y0; }
};

Bounds BoundsOf(float lo_x, float lo_y, float hi_x, float hi_y, float margin) {
  Bounds b;
  b.x0 = ClampInt(static_cast<int>(std::floor(lo_x - margin)), 0,
                  pixoo::kWidth - 1);
  b.x1 = ClampInt(static_cast<int>(std::ceil(hi_x + margin)), 0,
                  pixoo::kWidth - 1);
  b.y0 = ClampInt(static_cast<int>(std::floor(lo_y - margin)), 0,
                  pixoo::kHeight - 1);
  b.y1 = ClampInt(static_cast<int>(std::ceil(hi_y + margin)), 0,
                  pixoo::kHeight - 1);
  if (hi_x + margin < 0.0f || lo_x - margin > pixoo::kWidth - 1 ||
      hi_y + margin < 0.0f || lo_y - margin > pixoo::kHeight - 1)
    b.x1 = b.x0 - 1;
  return b;
}

Bounds UnionBounds(const Disc *discs, int count, const Rect *rect) {
  float lo_x = 0.0f, lo_y = 0.0f, hi_x = 0.0f, hi_y = 0.0f;
  bool any = false;
  for (int i = 0; i < count; i++) {
    const Disc &disc = discs[i];
    const float dx0 = disc.x - disc.r, dx1 = disc.x + disc.r;
    const float dy0 = disc.y - disc.r, dy1 = disc.y + disc.r;
    lo_x = any ? (dx0 < lo_x ? dx0 : lo_x) : dx0;
    hi_x = any ? (dx1 > hi_x ? dx1 : hi_x) : dx1;
    lo_y = any ? (dy0 < lo_y ? dy0 : lo_y) : dy0;
    hi_y = any ? (dy1 > hi_y ? dy1 : hi_y) : dy1;
    any = true;
  }
  if (rect != nullptr) {
    lo_x = any ? (rect->x0 < lo_x ? rect->x0 : lo_x) : rect->x0;
    hi_x = any ? (rect->x1 > hi_x ? rect->x1 : hi_x) : rect->x1;
    lo_y = any ? (rect->y0 < lo_y ? rect->y0 : lo_y) : rect->y0;
    hi_y = any ? (rect->y1 > hi_y ? rect->y1 : hi_y) : rect->y1;
    any = true;
  }
  if (!any)
    return Bounds{0, 0, -1, -1};
  return BoundsOf(lo_x, lo_y, hi_x, hi_y, 1.0f);
}

// Coverage from a signed distance: `inside` is how far the pixel center lies
// inside the edge, positive within the shape.
float EdgeCoverage(float inside) { return Clamp01(inside + 0.5f); }

}  // namespace

float Clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

float Lerp(float a, float b, float t) { return a + (b - a) * t; }

Color Blend(Color under, Color over, float coverage) {
  const float a = Clamp01(coverage);
  return Color(static_cast<uint8_t>(under.r + (over.r - under.r) * a),
               static_cast<uint8_t>(under.g + (over.g - under.g) * a),
               static_cast<uint8_t>(under.b + (over.b - under.b) * a));
}

void BlendAt(display::Display &d, BlendCanvas *canvas, int x, int y,
             Color color, float alpha) {
  if (alpha <= 0.0f || x < 0 || x >= pixoo::kWidth || y < 0 ||
      y >= pixoo::kHeight)
    return;
  if (canvas != nullptr)
    canvas->BlendPixel(x, y, color, alpha > 1.0f ? 1.0f : alpha);
  else if (alpha >= 0.5f)
    d.draw_pixel_at(x, y, color);
}

float DiscCoverage(float px, float py, const Disc &disc) {
  const float dx = px - disc.x;
  const float dy = py - disc.y;
  return EdgeCoverage(disc.r - std::sqrt(dx * dx + dy * dy));
}

float CapsuleCoverage(float px, float py, float x0, float y0, float x1,
                      float y1, float r0, float r1) {
  const float dx = x1 - x0;
  const float dy = y1 - y0;
  const float len2 = dx * dx + dy * dy;
  float t = 0.0f;
  if (len2 > 0.0f)
    t = Clamp01(((px - x0) * dx + (py - y0) * dy) / len2);
  const float ox = px - (x0 + t * dx);
  const float oy = py - (y0 + t * dy);
  return EdgeCoverage(Lerp(r0, r1, t) - std::sqrt(ox * ox + oy * oy));
}

float RectCoverage(float px, float py, const Rect &rect) {
  const float inside_x = std::fmin(px - rect.x0, rect.x1 - px);
  const float inside_y = std::fmin(py - rect.y0, rect.y1 - py);
  return EdgeCoverage(inside_x) * EdgeCoverage(inside_y);
}

float UnionCoverage(float px, float py, const Disc *discs, int count,
                    const Rect *rect) {
  // Coverage of the union, not the largest single coverage: where two parts
  // cross, each covers only some of the pixel while the union covers more of
  // it than either. Taking the maximum would leave a notch at every crossing.
  // Treating the parts as independently covering the pixel combines them
  // without needing their intersection.
  float uncovered = 1.0f;
  for (int i = 0; i < count; i++) {
    uncovered *= 1.0f - DiscCoverage(px, py, discs[i]);
    if (uncovered <= 0.0f)
      return 1.0f;
  }
  if (rect != nullptr)
    uncovered *= 1.0f - RectCoverage(px, py, *rect);
  return Clamp01(1.0f - uncovered);
}

void FillDisc(display::Display &d, BlendCanvas *canvas, const Disc &disc,
              Color color, float alpha) {
  const Bounds b = BoundsOf(disc.x - disc.r, disc.y - disc.r, disc.x + disc.r,
                            disc.y + disc.r, 1.0f);
  if (b.empty() || alpha <= 0.0f)
    return;
  for (int y = b.y0; y <= b.y1; y++) {
    for (int x = b.x0; x <= b.x1; x++)
      BlendAt(d, canvas, x, y, color,
              DiscCoverage(x + 0.5f, y + 0.5f, disc) * alpha);
  }
}

void FillDiscVertical(display::Display &d, BlendCanvas *canvas,
                      const Disc &disc, Color top, Color bottom, float alpha) {
  const Bounds b = BoundsOf(disc.x - disc.r, disc.y - disc.r, disc.x + disc.r,
                            disc.y + disc.r, 1.0f);
  if (b.empty() || alpha <= 0.0f || disc.r <= 0.0f)
    return;
  for (int y = b.y0; y <= b.y1; y++) {
    const float t = Clamp01((y + 0.5f - (disc.y - disc.r)) / (2.0f * disc.r));
    const Color row = Blend(top, bottom, t);
    for (int x = b.x0; x <= b.x1; x++)
      BlendAt(d, canvas, x, y, row,
              DiscCoverage(x + 0.5f, y + 0.5f, disc) * alpha);
  }
}

void FillDiscRadial(display::Display &d, BlendCanvas *canvas, const Disc &disc,
                    Color core, Color rim, float bias, float alpha) {
  const Bounds b = BoundsOf(disc.x - disc.r, disc.y - disc.r, disc.x + disc.r,
                            disc.y + disc.r, 1.0f);
  if (b.empty() || alpha <= 0.0f || disc.r <= 0.0f)
    return;
  for (int y = b.y0; y <= b.y1; y++) {
    for (int x = b.x0; x <= b.x1; x++) {
      const float dx = x + 0.5f - disc.x;
      const float dy = y + 0.5f - disc.y;
      const float dist = Clamp01(std::sqrt(dx * dx + dy * dy) / disc.r);
      BlendAt(d, canvas, x, y, Blend(core, rim, std::pow(dist, bias)),
              DiscCoverage(x + 0.5f, y + 0.5f, disc) * alpha);
    }
  }
}

void FillDiscGlow(display::Display &d, BlendCanvas *canvas, const Disc &disc,
                  Color color, float peak_alpha, float falloff,
                  float inner_r) {
  const Bounds b = BoundsOf(disc.x - disc.r, disc.y - disc.r, disc.x + disc.r,
                            disc.y + disc.r, 1.0f);
  if (b.empty() || peak_alpha <= 0.0f || disc.r <= inner_r)
    return;
  const float span = disc.r - inner_r;
  for (int y = b.y0; y <= b.y1; y++) {
    for (int x = b.x0; x <= b.x1; x++) {
      const float dx = x + 0.5f - disc.x;
      const float dy = y + 0.5f - disc.y;
      const float dist = std::sqrt(dx * dx + dy * dy);
      if (dist >= disc.r || dist < inner_r)
        continue;
      BlendAt(d, canvas, x, y, color,
              peak_alpha * std::pow(1.0f - (dist - inner_r) / span, falloff));
    }
  }
}

void StrokeCapsule(display::Display &d, BlendCanvas *canvas, float x0, float y0,
                   float x1, float y1, float r0, float r1, Color color,
                   float alpha) {
  const float r = r0 > r1 ? r0 : r1;
  const Bounds b = BoundsOf(std::fmin(x0, x1) - r, std::fmin(y0, y1) - r,
                            std::fmax(x0, x1) + r, std::fmax(y0, y1) + r, 1.0f);
  if (b.empty() || alpha <= 0.0f)
    return;
  for (int y = b.y0; y <= b.y1; y++) {
    for (int x = b.x0; x <= b.x1; x++)
      BlendAt(d, canvas, x, y, color,
              CapsuleCoverage(x + 0.5f, y + 0.5f, x0, y0, x1, y1, r0, r1) *
                  alpha);
  }
}

void FillUnionVertical(display::Display &d, BlendCanvas *canvas,
                       const Disc *discs, int count, const Rect *rect,
                       float top_y, float bottom_y, Color top, Color bottom,
                       float alpha) {
  const Bounds b = UnionBounds(discs, count, rect);
  if (b.empty() || alpha <= 0.0f)
    return;
  const float span = bottom_y - top_y;
  for (int y = b.y0; y <= b.y1; y++) {
    const float t = span > 0.0f ? Clamp01((y + 0.5f - top_y) / span) : 0.0f;
    const Color row = Blend(top, bottom, t);
    for (int x = b.x0; x <= b.x1; x++)
      BlendAt(d, canvas, x, y, row,
              UnionCoverage(x + 0.5f, y + 0.5f, discs, count, rect) * alpha);
  }
}

}  // namespace esphome::pixoo64::content
