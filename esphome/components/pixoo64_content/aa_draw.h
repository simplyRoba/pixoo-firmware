#pragma once

#include <cstddef>
#include <cstdint>

#include "blend_canvas.h"
#include "esphome/components/display/display.h"
#include "esphome/core/color.h"

namespace esphome::pixoo64::content {

// Anti-aliased shape drawing over a BlendCanvas. A shape covers a pixel by
// area rather than by a hit test, so an edge at any angle and a shape at any
// sub-pixel position read as a curve rather than a staircase, and a shape can
// move by less than a pixel.
//
// Every entry point takes the display plus the canvas the caller resolved
// through BlendCanvasOf(). Without a canvas a partially covered pixel cannot be
// mixed with what is under it, so coverage falls back to a threshold and the
// shape draws hard-edged.

float Clamp01(float v);
float Lerp(float a, float b, float t);
// `coverage` 0 keeps `under`, 1 replaces it with `over`.
Color Blend(Color under, Color over, float coverage);

// Mixes `color` into (x, y) at `alpha`. Out-of-range coordinates are ignored.
void BlendAt(display::Display &d, BlendCanvas *canvas, int x, int y,
             Color color, float alpha);

// A fixed pixel of a small sprite, its opacity scaled by the caller's fade.
struct ParticlePixel {
  int8_t x;
  int8_t y;
  uint8_t opacity;
};

template<size_t N>
void DrawParticleSprite(display::Display &d, BlendCanvas *canvas, int x, int y,
                        Color color, float fade,
                        const ParticlePixel (&pixels)[N]) {
  for (const ParticlePixel &pixel : pixels)
    BlendAt(d, canvas, x + pixel.x, y + pixel.y, color,
            pixel.opacity / 255.0f * fade);
}

struct Disc {
  float x{0.0f};
  float y{0.0f};
  float r{0.0f};
};

struct Rect {
  float x0{0.0f};
  float y0{0.0f};
  float x1{0.0f};
  float y1{0.0f};
};

struct Triangle {
  float x0{0.0f};
  float y0{0.0f};
  float x1{0.0f};
  float y1{0.0f};
  float x2{0.0f};
  float y2{0.0f};
};

// Fraction of the pixel whose center is (px, py) inside the shape, taking the
// edge to be one pixel wide.
float DiscCoverage(float px, float py, const Disc &disc);
// The sweep of a disc whose radius runs from `r0` at (x0, y0) to `r1` at
// (x1, y1): a line with rounded, tapering ends.
float CapsuleCoverage(float px, float py, float x0, float y0, float x1,
                      float y1, float r0, float r1);
float RectCoverage(float px, float py, const Rect &rect);
// A convex triangle, covered by how far the pixel lies inside all three edges.
float TriangleCoverage(float px, float py, const Triangle &triangle);
// The union of `count` discs and an optional rectangle, as the strongest
// coverage of any of them. A union has no internal seams, so one silhouette
// can be built from overlapping parts and shaded as a whole.
float UnionCoverage(float px, float py, const Disc *discs, int count,
                    const Rect *rect);

// `alpha` scales the coverage of every pixel, so a whole shape can fade.
void FillDisc(display::Display &d, BlendCanvas *canvas, const Disc &disc,
              Color color, float alpha = 1.0f);
void FillTriangle(display::Display &d, BlendCanvas *canvas,
                  const Triangle &triangle, Color color, float alpha = 1.0f);
// Vertical gradient: `top` at the top of the disc, `bottom` at its bottom.
void FillDiscVertical(display::Display &d, BlendCanvas *canvas,
                      const Disc &disc, Color top, Color bottom,
                      float alpha = 1.0f);
// Radial gradient: `core` at the center, `rim` at the edge. `bias` above 1
// holds the core color further out before falling to the rim.
void FillDiscRadial(display::Display &d, BlendCanvas *canvas, const Disc &disc,
                    Color core, Color rim, float bias = 2.0f,
                    float alpha = 1.0f);
// A soft halo: `peak_alpha` at `inner_r` and falling to nothing at the rim, so
// it reads as light around a body rather than a second disc. `falloff` above 1
// keeps the halo tight. Inside `inner_r` nothing is drawn, so a halo around a
// body that is not a full disc does not fill the gaps in it.
void FillDiscGlow(display::Display &d, BlendCanvas *canvas, const Disc &disc,
                  Color color, float peak_alpha, float falloff = 2.0f,
                  float inner_r = 0.0f);
void StrokeCapsule(display::Display &d, BlendCanvas *canvas, float x0, float y0,
                   float x1, float y1, float r0, float r1, Color color,
                   float alpha = 1.0f);
// One vertical gradient over a whole union silhouette, running from `top_y` to
// `bottom_y` in panel coordinates.
void FillUnionVertical(display::Display &d, BlendCanvas *canvas,
                       const Disc *discs, int count, const Rect *rect,
                       float top_y, float bottom_y, Color top, Color bottom,
                       float alpha = 1.0f);

}  // namespace esphome::pixoo64::content
