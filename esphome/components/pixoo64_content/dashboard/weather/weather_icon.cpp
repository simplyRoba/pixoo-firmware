#include "weather_icon.h"

#include <cmath>

#include "esphome/components/pixoo64_content/aa_draw.h"
#include "esphome/components/pixoo64_content/blend_canvas.h"

namespace esphome::pixoo64::weather {
namespace {

namespace content = ::esphome::pixoo64::content;

using content::BlendAt;
using content::BlendCanvas;
using content::Clamp01;
using content::Disc;
using content::DiscCoverage;
using content::FillDisc;
using content::FillDiscGlow;
using content::FillDiscRadial;
using content::FillDiscVertical;
using content::FillTriangle;
using content::FillUnionVertical;
using content::Lerp;
using content::Rect;
using content::StrokeCapsule;
using content::Triangle;
using content::UnionCoverage;

constexpr float kPi = 3.14159265358979323846f;
constexpr float kTau = 2.0f * kPi;

Color Mix(Color a, Color b, float t) { return content::Blend(a, b, t); }

// ---- animation timing ------------------------------------------------------

// Position within a loop, 0 at its start and approaching 1 at its end.
float Phase01(uint64_t elapsed_ms, uint32_t period_ms) {
  if (period_ms == 0)
    return 0.0f;
  return static_cast<float>(elapsed_ms % period_ms) /
         static_cast<float>(period_ms);
}

float Frac(float v) { return v - std::floor(v); }

// A full sine turn over one loop, so a value driven by it meets itself at the
// period boundary.
float Wave(float phase01) { return std::sin(phase01 * kTau); }

// Ramps in over the first `in` of its travel and out over the last `out`, so a
// particle entering or leaving a band is never a pixel appearing from nothing.
float TravelFade(float t, float in, float out) {
  const float rise = in > 0.0f ? Clamp01(t / in) : 1.0f;
  const float fall = out > 0.0f ? Clamp01((1.0f - t) / out) : 1.0f;
  return rise < fall ? rise : fall;
}

// ---- shared shapes ---------------------------------------------------------

// A lit crescent: the disc at [cx,cy] minus an equal disc offset by [ox,oy].
// One coverage per pixel from both edges keeps the limb smooth and the horns
// attached, and the offset direction sets which way the lit limb faces.
void DrawCrescent(display::Display &d, BlendCanvas *canvas, float cx, float cy,
                  float r, float ox, float oy, Color top, Color bottom,
                  float alpha = 1.0f) {
  if (r <= 0.0f || alpha <= 0.0f)
    return;
  const int x0 = static_cast<int>(std::floor(cx - r - 1.0f));
  const int x1 = static_cast<int>(std::ceil(cx + r + 1.0f));
  const int y0 = static_cast<int>(std::floor(cy - r - 1.0f));
  const int y1 = static_cast<int>(std::ceil(cy + r + 1.0f));
  const Disc body{cx, cy, r};
  const Disc cut{cx + ox, cy + oy, r};
  for (int y = y0; y <= y1; y++) {
    const float t = Clamp01((y + 0.5f - (cy - r)) / (2.0f * r));
    const Color row = Mix(top, bottom, t);
    for (int x = x0; x <= x1; x++) {
      const float px = x + 0.5f, py = y + 0.5f;
      const float cov =
          DiscCoverage(px, py, body) * (1.0f - DiscCoverage(px, py, cut));
      BlendAt(d, canvas, x, y, row, cov * alpha);
    }
  }
}

// The canonical cloud silhouette: a flat bottom under three rounded bumps, the
// central one largest and right of center. Drawn as one union so the lumps
// leave no internal seam, under a single vertical gradient that keeps the crown
// bright and deepens the underside. `swell` scales each bump on its own, which
// billows the outline without moving the cloud.
struct CloudSwell {
  float left{1.0f};
  float center{1.0f};
  float right{1.0f};
};

struct CloudShape {
  Disc lumps[3];
  Rect base;
  float top_y{0.0f};
  float bottom_y{0.0f};
};

CloudShape MakeCloud(float cx, float cy, float w, const CloudSwell &swell) {
  CloudShape shape;
  const float rc = std::fmax(3.0f, w * 0.52f) * swell.center;
  const float rl = std::fmax(2.0f, w * 0.28f) * swell.left;
  const float rr = std::fmax(2.0f, w * 0.34f) * swell.right;
  // The flat underside falls on a pixel boundary. Landing mid-pixel would
  // leave the last row at partial coverage, which draws as a lighter rule
  // under the cloud rather than as its edge.
  shape.bottom_y = std::floor(cy + w / 5.0f);
  // Every bump rests on that underside instead of hanging below it, so the
  // three read as one cloud rather than as loose balls, and a swelling bump
  // grows upward from the base rather than drifting off it. The bumps differ
  // in size and height and none is centered: a cloud is a heap, so a
  // symmetric arrangement reads as geometry instead.
  shape.lumps[0] = Disc{cx - w * 0.64f, shape.bottom_y - rl, rl};
  shape.lumps[1] = Disc{cx - w * 0.06f, shape.bottom_y - rc * 1.10f, rc};
  shape.lumps[2] = Disc{cx + w * 0.56f, shape.bottom_y - rr * 0.92f, rr};
  // The base reaches up to the smallest bump's crown, so the body behind the
  // bumps is solid. A base shallower than that leaves the dips between two
  // bumps only partly covered, which draws as holes punched in the cloud
  // rather than as its underside.
  shape.base = Rect{cx - w * 0.90f, shape.bottom_y - rl * 1.05f,
                    cx + w * 0.88f, shape.bottom_y};
  shape.top_y = shape.bottom_y;
  for (const Disc &lump : shape.lumps)
    shape.top_y = std::fmin(shape.top_y, lump.y - lump.r);
  return shape;
}

void DrawCloudShape(display::Display &d, BlendCanvas *canvas,
                    const CloudShape &shape, bool dark, float alpha = 1.0f,
                    float lit = 0.0f) {
  const Color top = dark ? Color(150, 156, 170) : Color(240, 244, 252);
  const Color bot = dark ? Color(70, 74, 88) : Color(150, 160, 180);
  if (alpha <= 0.0f)
    return;
  float lo_x = shape.base.x0, hi_x = shape.base.x1;
  for (const Disc &lump : shape.lumps) {
    lo_x = std::fmin(lo_x, lump.x - lump.r);
    hi_x = std::fmax(hi_x, lump.x + lump.r);
  }
  const int x0 = static_cast<int>(std::floor(lo_x - 1.0f));
  const int x1 = static_cast<int>(std::ceil(hi_x + 1.0f));
  const int y0 = static_cast<int>(std::floor(shape.top_y - 1.0f));
  const int y1 = static_cast<int>(std::ceil(shape.bottom_y + 1.0f));
  const float span = shape.bottom_y - shape.top_y;
  for (int y = y0; y <= y1; y++) {
    float t = span > 0.0f ? Clamp01((y + 0.5f - shape.top_y) / span) : 0.0f;
    t = t * t;  // ease-in: hold the crown bright, deepen the shadow lower down
    Color row = Mix(top, bot, t);
    // A bolt lights the cloud from below, so the underside brightens most.
    if (lit > 0.0f)
      row = Mix(row, Color(255, 236, 190), lit * t * 0.85f);
    for (int x = x0; x <= x1; x++) {
      const float cov = UnionCoverage(x + 0.5f, y + 0.5f, shape.lumps, 3,
                                      &shape.base);
      BlendAt(d, canvas, x, y, row, cov * alpha);
    }
  }
  // Bright rim along the crown of the central bump, where the light falls.
  // Y grows downward, so the upper-left arc is the negative-sine half. The
  // highlight only lands where that arc is the cloud's outer edge; inside the
  // silhouette it would read as a scratch rather than a lit rim.
  const Color hi = dark ? Color(200, 206, 220) : Color(255, 255, 255);
  const Disc &center = shape.lumps[1];
  for (float a = 200.0f; a <= 310.0f; a += 4.0f) {
    const float rad = a * kPi / 180.0f;
    const float hx = center.x + std::cos(rad) * (center.r - 0.45f);
    const float hy = center.y + std::sin(rad) * (center.r - 0.45f);
    // Just outside this point the cloud must end, or the point is interior.
    const float ox = center.x + std::cos(rad) * (center.r + 0.9f);
    const float oy = center.y + std::sin(rad) * (center.r + 0.9f);
    if (UnionCoverage(ox, oy, shape.lumps, 3, &shape.base) > 0.35f)
      continue;
    FillDisc(d, canvas, Disc{hx, hy, 0.5f}, hi, 0.5f * alpha);
  }
}

void DrawCloud(display::Display &d, BlendCanvas *canvas, float cx, float cy,
               float w, bool dark, const CloudSwell &swell = {},
               float lit = 0.0f) {
  DrawCloudShape(d, canvas, MakeCloud(cx, cy, w, swell), dark, 1.0f, lit);
}

// The sun: a warm radial body under a soft halo, ringed by rays that breathe in
// and out. The axis and diagonal rays run on opposite phases, so the corona
// pulses rather than scaling as one piece.
void DrawSun(display::Display &d, BlendCanvas *canvas, float cx, float cy,
             float r, bool rays, const IconAnimation &anim) {
  const float breath = Wave(Phase01(anim.elapsed_ms, 3000u));
  if (rays) {
    const Color ray(255, 190, 30);
    // The rays breathe within the icon's box rather than out of it. At full
    // extension the furthest lit pixel is the gap, plus the ray, plus the
    // tip's own radius and the pixel its anti-aliased edge reaches into: all
    // of that has to stay inside the box half-extent the caller sized the icon
    // to, or a ray draws over the clock above it.
    const float len = r * 0.34f;
    const float tip_r = 0.35f;
    for (int k = 0; k < 8; k++) {
      const bool axis = (k % 2) == 0;
      const float gap = r + 0.9f + (axis ? breath : -breath) * 0.30f;
      const float rad = k * 45.0f * kPi / 180.0f;
      const float ux = std::sin(rad), uy = -std::cos(rad);
      StrokeCapsule(d, canvas, cx + ux * gap, cy + uy * gap,
                    cx + ux * (gap + len), cy + uy * (gap + len), 0.6f, tip_r,
                    ray);
    }
  }
  FillDiscGlow(d, canvas, Disc{cx, cy, r * 1.55f}, Color(255, 168, 72),
               0.20f + 0.06f * breath, 2.4f);
  FillDiscRadial(d, canvas, Disc{cx, cy, r}, Color(255, 240, 170),
                 Color(255, 150, 20), 2.0f);
}

// The moon: a crescent whose limb glow breathes, with two stars that twinkle
// beside it on their own cycles.
void DrawMoonBody(display::Display &d, BlendCanvas *canvas, float cx, float cy,
                  float r, float ox, float oy, const IconAnimation &anim,
                  bool glow) {
  // Light comes off the lit crescent, so the halo is the crescent's own shape
  // grown outward: the dark side neither fills with a wash nor gets outlined.
  if (glow) {
    const float breath = Wave(Phase01(anim.elapsed_ms, 4000u));
    const float peak = 0.26f + 0.08f * breath;
    // The halo reaches beyond the disc, so a caller sizes the moon for the
    // body and this glow together to fit the space the icon is given.
    const float reach = r * 0.42f;
    const Disc lit{cx, cy, r};
    const Disc cut{cx + ox, cy + oy, r};
    const int x0 = static_cast<int>(std::floor(cx - r - reach - 1.0f));
    const int x1 = static_cast<int>(std::ceil(cx + r + reach + 1.0f));
    const int y0 = static_cast<int>(std::floor(cy - r - reach - 1.0f));
    const int y1 = static_cast<int>(std::ceil(cy + r + reach + 1.0f));
    const Color halo(206, 216, 176);
    // The lit limb faces away from the cut, so the halo is strongest on that
    // side and fades to nothing around the back of the dark side.
    const float olen = std::sqrt(ox * ox + oy * oy);
    const float lx = olen > 0.0f ? -ox / olen : 0.0f;
    const float ly = olen > 0.0f ? -oy / olen : 0.0f;
    for (int y = y0; y <= y1; y++) {
      for (int x = x0; x <= x1; x++) {
        const float px = x + 0.5f, py = y + 0.5f;
        const float dx = px - lit.x, dy = py - lit.y;
        const float dist = std::sqrt(dx * dx + dy * dy);
        const float outside = dist - lit.r;
        if (outside <= 0.0f || outside >= reach || dist <= 0.0f)
          continue;
        const float facing = Clamp01((dx * lx + dy * ly) / dist);
        if (facing <= 0.0f)
          continue;
        BlendAt(d, canvas, x, y, halo,
                peak * facing * std::pow(1.0f - outside / reach, 1.8f));
      }
    }
  }
  DrawCrescent(d, canvas, cx, cy, r, ox, oy, Color(245, 245, 205),
               Color(200, 205, 150));
}

struct Star {
  float x;
  float y;
  uint32_t period_ms;
};

void DrawStars(display::Display &d, BlendCanvas *canvas, float cx, float cy,
               float s, const IconAnimation &anim) {
  // Each star carries a halo, so it sits far enough inside the icon's box for
  // that halo to fit as well; a star placed at the box edge lights pixels
  // outside it, over the layout's text.
  const Star stars[2] = {{cx - s * 0.72f, cy - s * 0.56f, 2600u},
                         {cx - s * 0.48f, cy + s * 0.58f, 3700u}};
  for (const Star &star : stars) {
    const float twinkle = 0.5f + 0.5f * Wave(Phase01(anim.elapsed_ms,
                                                     star.period_ms));
    const Color light(240, 244, 255);
    FillDiscGlow(d, canvas, Disc{star.x, star.y, 2.1f}, light,
                 0.18f * twinkle, 2.2f);
    FillDisc(d, canvas, Disc{star.x, star.y, 0.72f}, light,
             0.45f + 0.55f * twinkle);
  }
}

// ---- precipitation ---------------------------------------------------------

// A field of falling streaks between `top` and `top + height`. Each column runs
// on its own offset within one shared period, so the field never pulses as a
// row, and every streak fades in as it enters and out as it lands.
void DrawRain(display::Display &d, BlendCanvas *canvas, float cx, float top,
              float w, int count, bool heavy, const IconAnimation &anim,
              float height) {
  const Color head(150, 205, 255);
  const Color tail(40, 110, 220);
  const float step = std::fmax(2.0f, (2.0f * w) / (count + 1));
  const float start = cx - step * (count - 1) / 2.0f;
  const float len = heavy ? 3.4f : 2.6f;
  const uint32_t period = heavy ? 620u : 900u;
  const float base = Phase01(anim.elapsed_ms, period);
  for (int i = 0; i < count; i++) {
    const float x = start + i * step;
    const float t = Frac(base + i * 0.41f);
    const float y = top + t * height;
    const float fade = TravelFade(t, 0.18f, 0.24f);
    StrokeCapsule(d, canvas, x, y, x, y + len, 0.42f, 0.62f,
                  Mix(head, tail, 0.55f), fade);
    FillDisc(d, canvas, Disc{x, y + len, 0.55f}, head, fade * 0.85f);
    if (heavy) {
      // A landed drop throws a short splash as the next one enters.
      const float splash = Clamp01((t - 0.82f) / 0.18f);
      if (splash > 0.0f) {
        const float sy = top + height + 0.5f;
        FillDisc(d, canvas, Disc{x - 1.2f * splash, sy, 0.42f}, head,
                 (1.0f - splash) * 0.6f);
        FillDisc(d, canvas, Disc{x + 1.2f * splash, sy, 0.42f}, head,
                 (1.0f - splash) * 0.6f);
      }
    }
  }
}

// Intermittent single drops rather than streaks, so light precipitation reads
// apart from rain.
void DrawDrizzle(display::Display &d, BlendCanvas *canvas, float cx, float top,
                 float w, int count, const IconAnimation &anim, float height) {
  const Color c(120, 190, 255);
  const float step = std::fmax(2.0f, (2.0f * w) / (count + 1));
  const float start = cx - step * (count - 1) / 2.0f;
  const float base = Phase01(anim.elapsed_ms, 1400u);
  for (int i = 0; i < count; i++) {
    const float x = start + i * step;
    for (int k = 0; k < 2; k++) {
      const float t = Frac(base + i * 0.33f + k * 0.5f);
      const float y = top + t * height;
      FillDisc(d, canvas, Disc{x, y, 0.58f}, c, TravelFade(t, 0.2f, 0.3f));
    }
  }
}

// Grains are small and dense, and wander sideways as they fall.
void DrawGrains(display::Display &d, BlendCanvas *canvas, float cx, float top,
                float w, int count, const IconAnimation &anim, float height) {
  const Color c(235, 245, 255);
  const float step = std::fmax(2.0f, (2.0f * w) / (count + 1));
  const float start = cx - step * (count - 1) / 2.0f;
  const float base = Phase01(anim.elapsed_ms, 2000u);
  for (int i = 0; i < count; i++) {
    const float t = Frac(base + i * 0.27f);
    const float drift = Wave(Frac(t + i * 0.19f)) * 0.8f;
    const float x = start + i * step + drift;
    FillDisc(d, canvas, Disc{x, top + t * height, 0.5f}, c,
             TravelFade(t, 0.18f, 0.28f));
  }
}

// Ice pellets fall slower than the liquid beside them and flash as they land,
// which is what marks a freezing type apart from its liquid one.
void DrawIcePellets(display::Display &d, BlendCanvas *canvas, float cx,
                    float top, float w, int count, const IconAnimation &anim,
                    float height) {
  const Color top_c(245, 250, 255);
  const Color bot_c(185, 205, 235);
  const float step = std::fmax(2.0f, (2.0f * w) / (count + 1));
  const float start = cx - step * (count - 1) / 2.0f;
  const float base = Phase01(anim.elapsed_ms, 1500u);
  for (int i = 0; i < count; i++) {
    const float x = start + i * step;
    const float t = Frac(base + i * 0.47f);
    const float y = top + t * height;
    const float fade = TravelFade(t, 0.16f, 0.2f);
    FillDiscVertical(d, canvas, Disc{x, y, 1.05f}, top_c, bot_c, fade);
    const float land = Clamp01((t - 0.80f) / 0.20f);
    if (land > 0.0f)
      FillDiscGlow(d, canvas, Disc{x, top + height + 0.6f, 1.8f}, top_c,
                   (1.0f - land) * 0.55f, 1.6f);
  }
}

// ---- lightning -------------------------------------------------------------

// The bolt stays drawn between strikes, dimmer and cooler, so the icon still
// names the condition while the schedule drives the flash.
constexpr float kBoltRestAlpha = 0.42f;

void DrawBolt(display::Display &d, BlendCanvas *canvas, float cx, float cy,
              float h, float strike) {
  const float w = std::fmax(2.0f, h / 2.0f);
  const float alpha = kBoltRestAlpha + (1.0f - kBoltRestAlpha) * strike;
  const Color amber = Mix(Color(214, 138, 22), Color(255, 186, 40), strike);
  const Color hot = Mix(Color(255, 214, 120), Color(255, 252, 214), strike);
  if (strike > 0.0f)
    FillDiscGlow(d, canvas, Disc{cx, cy, h * 1.5f}, Color(255, 228, 150),
                 0.42f * strike, 2.0f);
  FillTriangle(d, canvas,
               Triangle{cx + 0.6f, cy - h, cx - w, cy + h / 4.0f, cx + 0.4f,
                        cy},
               amber, alpha);
  FillTriangle(d, canvas,
               Triangle{cx - 0.4f, cy, cx + w, cy - h / 4.0f, cx - 0.6f,
                        cy + h},
               amber, alpha);
  StrokeCapsule(d, canvas, cx + 0.2f, cy - h + 1.0f, cx - 0.9f, cy, 0.45f,
                0.35f, hot, alpha);
  StrokeCapsule(d, canvas, cx + 0.2f, cy, cx - 0.4f, cy + h - 1.0f, 0.4f, 0.3f,
                hot, alpha);
}

// ---- fog -------------------------------------------------------------------

// Four banks sliding at their own speeds and amplitudes, which reads as a
// layered drift rather than one block moving.
void DrawFogLines(display::Display &d, BlendCanvas *canvas, float cx, float cy,
                  float w, float h, const IconAnimation &anim) {
  const Color a(210, 214, 224);
  const Color b(120, 126, 140);
  const uint32_t periods[4] = {5200u, 6800u, 4400u, 7600u};
  for (int i = 0; i < 4; i++) {
    const float y = cy - h + i * (2.0f * h / 3.0f);
    const float inset = (i % 2) * (w / 3.0f);
    const float drift = Wave(Phase01(anim.elapsed_ms, periods[i])) *
                        (1.1f + 0.35f * i);
    const Color c = Mix(a, b, i / 3.0f);
    StrokeCapsule(d, canvas, cx - w + inset + drift, y, cx + w + drift, y,
                  0.62f, 0.62f, c);
  }
}

// ---- snow crystal ----------------------------------------------------------

// Six-fold ice symmetry: three spokes through the center, each with a pair of
// dendrite branches. A sixth of a turn per loop returns the crystal to itself,
// so it rotates without ever restarting.
void DrawSnowflake(display::Display &d, BlendCanvas *canvas, float cx, float cy,
                   float r, const IconAnimation &anim) {
  const Color a(230, 244, 255);
  const Color b(165, 205, 242);
  const float rot = Phase01(anim.elapsed_ms, 12000u) * 60.0f;
  const float bob = Wave(Phase01(anim.elapsed_ms, 5000u)) * 0.45f;
  const float y = cy + bob;
  for (int i = 0; i < 3; i++) {
    const float ang = (i * 60.0f + rot) * kPi / 180.0f;
    const float cs = std::cos(ang), sn = std::sin(ang);
    const float ex = cs * r, ey = sn * r;
    StrokeCapsule(d, canvas, cx - ex, y - ey, cx + ex, y + ey, 0.55f, 0.55f, a);
    const float bl = r * 0.32f;
    for (int s = -1; s <= 1; s += 2) {
      const float mx = cs * r * 0.68f * s;
      const float my = sn * r * 0.68f * s;
      for (int t = -1; t <= 1; t += 2) {
        const float ba = ang + s * t * 60.0f * kPi / 180.0f;
        StrokeCapsule(d, canvas, cx + mx, y + my,
                      cx + mx + std::cos(ba) * bl * s,
                      y + my + std::sin(ba) * bl * s, 0.42f, 0.32f, b);
      }
    }
  }
  FillDisc(d, canvas, Disc{cx, y, std::fmax(1.0f, r / 5.0f)}, a);
}

}  // namespace

// ---- hero (large, detailed, colorful) --------------------------------------

void DrawWeatherIconHero(display::Display &d, WeatherCondition c, bool night,
                         int cx_i, int cy_i, int s_i, const IconAnimation &a) {
  BlendCanvas *canvas = content::BlendCanvasOf(d);
  const float cx = cx_i, cy = cy_i, s = s_i;
  // Precipitation falls from the cloud's underside to the icon's lower edge.
  // The cloud hangs far enough below the top of the box for its crown, at the
  // widest point of its swell and with its anti-aliased edge, to stay inside.
  const float cloud_w = s * 0.8f;
  const float cloud_cy = cy - s / 6.0f + 0.5f;
  const float band_top = cloud_cy + cloud_w / 5.0f + 1.0f;
  // A particle's own body hangs below the position it is drawn at: a rain
  // streak trails behind its head and lands with a splash. The band it falls
  // through therefore ends short of the icon's box by that much, so the whole
  // fall stays inside the space the layout gives the icon instead of running
  // into the row of statistics under it.
  constexpr float kParticleTail = 4.0f;
  const float band_h = cy + s - band_top - kParticleTail;
  const bool storm = c == WeatherCondition::THUNDERSTORM ||
                     c == WeatherCondition::HAIL_THUNDERSTORM;
  const float lit = storm ? a.lightning : 0.0f;
  // Slow, out-of-phase swelling of the three bumps billows a cloud in place.
  const CloudSwell swell{
      1.0f + 0.035f * Wave(Phase01(a.elapsed_ms, 7000u)),
      1.0f + 0.030f * Wave(Frac(Phase01(a.elapsed_ms, 7000u) + 0.37f)),
      1.0f + 0.035f * Wave(Frac(Phase01(a.elapsed_ms, 7000u) + 0.68f))};

  switch (c) {
    case WeatherCondition::SUNNY:
      if (night) {
        DrawStars(d, canvas, cx, cy, s, a);
        DrawMoonBody(d, canvas, cx + 2.0f, cy, s * 0.66f, s * 0.33f,
                     -s * 0.17f, a, true);
      } else {
        DrawSun(d, canvas, cx, cy, s * 0.6f, true, a);
      }
      break;
    case WeatherCondition::PARTLYCLOUDY: {
      // The body sits upper-right and the cloud drifts across it lower-left.
      // The moon is cut shallow: the cloud hides the tail, and a thin sickle
      // would leave only a sliver visible.
      const float drift = Wave(Phase01(a.elapsed_ms, 6000u)) * 0.9f;
      if (night) {
        const float r = s * 0.4f;
        DrawMoonBody(d, canvas, cx + s / 6.0f, cy - s / 4.0f, r, r, -r / 4.0f,
                     a, true);
      } else {
        DrawSun(d, canvas, cx + s / 6.0f, cy - s / 4.0f, s * 0.4f, true, a);
      }
      DrawCloud(d, canvas, cx - s / 6.0f + drift, cy + s * 2.0f / 3.0f,
                s * 2.0f / 3.0f, false, swell);
      break;
    }
    case WeatherCondition::CLOUDY:
      DrawCloud(d, canvas, cx, cy + s / 3.0f, s, false, swell);
      break;
    case WeatherCondition::FOG:
      DrawCloud(d, canvas, cx, cy - s / 3.0f, s * 0.6f, false, swell);
      DrawFogLines(d, canvas, cx, cy + s / 2.0f, s * 0.75f, s / 3.0f, a);
      break;
    case WeatherCondition::DRIZZLE:
      DrawCloud(d, canvas, cx, cloud_cy, cloud_w, false, swell);
      DrawDrizzle(d, canvas, cx, band_top, s * 0.6f, 3, a, band_h);
      break;
    case WeatherCondition::FREEZING_DRIZZLE:
      DrawCloud(d, canvas, cx, cloud_cy, cloud_w, false, swell);
      DrawDrizzle(d, canvas, cx - s / 4.0f, band_top, s / 3.0f, 2, a, band_h);
      DrawIcePellets(d, canvas, cx + s / 3.0f, band_top, s / 4.0f, 2, a,
                     band_h - 1.0f);
      break;
    case WeatherCondition::RAINY:
      DrawCloud(d, canvas, cx, cloud_cy, cloud_w, false, swell);
      DrawRain(d, canvas, cx, band_top, s * 0.6f, 3, false, a, band_h);
      break;
    case WeatherCondition::POURING:
      DrawCloud(d, canvas, cx, cloud_cy, cloud_w, true, swell);
      DrawRain(d, canvas, cx, band_top, s * 0.75f, 4, true, a, band_h);
      break;
    case WeatherCondition::FREEZING_RAIN:
      DrawCloud(d, canvas, cx, cloud_cy, cloud_w, false, swell);
      DrawRain(d, canvas, cx - s / 3.0f, band_top, s / 3.0f, 2, false, a,
               band_h);
      DrawIcePellets(d, canvas, cx + s / 3.0f, band_top, s / 4.0f, 2, a,
                     band_h - 1.0f);
      break;
    case WeatherCondition::SNOWY:
      // An ice crystal, no cloud (this also sets it apart from the others).
      DrawSnowflake(d, canvas, cx, cy, s * 0.8f, a);
      break;
    case WeatherCondition::SNOW_GRAINS:
      DrawCloud(d, canvas, cx, cloud_cy, cloud_w, false, swell);
      DrawGrains(d, canvas, cx, band_top, s * 0.6f, 4, a, band_h);
      break;
    case WeatherCondition::THUNDERSTORM:
    case WeatherCondition::HAIL_THUNDERSTORM: {
      const bool hail = c == WeatherCondition::HAIL_THUNDERSTORM;
      DrawCloud(d, canvas, cx, cloud_cy, cloud_w, true, swell, lit);
      // The precipitation keeps clear of the bolt on both sides.
      const float spread = std::fmax(2.0f, s / 4.0f) + 3.0f;
      if (hail) {
        DrawIcePellets(d, canvas, cx - spread, band_top, 0.5f, 1, a,
                       band_h - 1.0f);
        DrawIcePellets(d, canvas, cx + spread, band_top + 1.0f, 0.5f, 1, a,
                       band_h - 2.0f);
      } else {
        DrawRain(d, canvas, cx - spread, band_top, 0.5f, 1, false, a, band_h);
        DrawRain(d, canvas, cx + spread, band_top + 1.0f, 0.5f, 1, false, a,
                 band_h - 1.0f);
      }
      DrawBolt(d, canvas, cx, cy + s / 4.0f, s / 2.0f, a.lightning);
      break;
    }
    case WeatherCondition::UNKNOWN:
    default:
      DrawCloud(d, canvas, cx, cy, s * 0.8f, false, swell);
      break;
  }
}

// ---- mini (small, crisp, purpose-drawn for the forecast strip) -------------

namespace {

// A compact cloud for the mini icons. At this size the silhouette carries the
// whole read, so the shape stays wider than it is tall and keeps a flat base
// under bumps of three different heights: a symmetric dome, or one bump wide
// enough to cover the base, reads as a ball instead of a cloud. Roughly
// (2*w+1) wide. The outline is anti-aliased; everything drawn against it stays
// on whole pixels.
void MiniCloud(display::Display &d, BlendCanvas *canvas, float cx, float cy,
               float w, bool dark) {
  const Color body = dark ? Color(120, 126, 140) : Color(225, 230, 240);
  const Color under = dark ? Color(96, 101, 114) : Color(188, 196, 212);
  // The lumps scale with the width so the proportions hold at every size the
  // strip uses, while the flat base keeps a fixed depth: the silhouette is all
  // there is to read at this size, and a dome that grows as fast as the width
  // closes the outline into a ball. The base falls on a pixel boundary so the
  // last row is the cloud's edge rather than a partial-coverage rule under it,
  // and the bumps differ in size and height so the heap is not symmetric.
  const float base_y = std::floor(cy + 1.2f);
  const Disc lumps[3] = {{cx - w * 0.66f, base_y - w * 0.30f, w * 0.30f},
                         {cx - w * 0.08f, base_y - w * 0.50f, w * 0.46f},
                         {cx + w * 0.58f, base_y - w * 0.34f, w * 0.36f}};
  const Rect base{cx - w + 0.2f, base_y - w * 0.26f, cx + w - 0.3f, base_y};
  FillUnionVertical(d, canvas, lumps, 3, &base, base_y - w * 0.96f, base_y,
                    body, under);
}

// A classic tiny lightning bolt. Two near-vertical strokes joined by a kink,
// with the lower stroke offset left of the upper one (they are not stacked):
//   . #      row0: upper stroke
//   . #      row1
//   # #      row2: kink spans both columns
//   # .      row3: lower stroke (shifted left)
//   # .      row4
// Top-right of the bolt at [cx+1, y].
void MiniBolt(display::Display &d, int cx, int y, Color c) {
  d.draw_pixel_at(cx + 1, y, c);
  d.draw_pixel_at(cx + 1, y + 1, c);
  d.draw_pixel_at(cx + 1, y + 2, c);
  d.draw_pixel_at(cx, y + 2, c);
  d.draw_pixel_at(cx, y + 3, c);
  d.draw_pixel_at(cx, y + 4, c);
}

}  // namespace

void DrawWeatherIconMini(display::Display &d, WeatherCondition c, bool night,
                         int cx, int cy, int s) {
  BlendCanvas *canvas = content::BlendCanvasOf(d);
  const Color sun(255, 200, 40);
  const Color moon(220, 226, 170);
  const Color rain(80, 170, 255);
  const Color snow(230, 244, 255);
  const Color bolt(255, 215, 40);
  const Color fog(170, 176, 190);
  const float fs = s;
  // A whole-pixel drawing call fills pixel [cx], whose middle is at cx + 0.5,
  // while a coverage-shaded shape is placed in continuous coordinates, where
  // cx is the boundary between two pixels. Anti-aliased parts therefore sit on
  // the pixel centre, so they share an axis with the pixel-art parts around
  // them instead of straddling the neighbouring pixel.
  const float fx = cx + 0.5f;
  const float fy = cy + 0.5f;
  switch (c) {
    case WeatherCondition::SUNNY: {
      if (night) {
        DrawCrescent(d, canvas, fx, fy, fs - 1.0f, fs / 2.0f, -fs / 2.0f, moon,
                     moon);
        break;
      }
      // A round core with a crisp ray cross: the disc carries the smoothing,
      // the rays stay single pixels so they keep their point.
      FillDisc(d, canvas, Disc{fx, fy,
                               fs * 0.62f},
               sun);
      FillDisc(d, canvas, Disc{fx, fy,
                               fs * 0.30f},
               Color(255, 232, 140));
      d.draw_pixel_at(cx, cy - 4, sun);
      d.draw_pixel_at(cx, cy + 4, sun);
      d.draw_pixel_at(cx - 4, cy, sun);
      d.draw_pixel_at(cx + 4, cy, sun);
      d.draw_pixel_at(cx - 3, cy - 3, sun);
      d.draw_pixel_at(cx + 3, cy - 3, sun);
      d.draw_pixel_at(cx - 3, cy + 3, sun);
      d.draw_pixel_at(cx + 3, cy + 3, sun);
      break;
    }
    case WeatherCondition::PARTLYCLOUDY:
      if (night) {
        // The crescent's lit limb is its lower-left, the part the cloud would
        // cover, so here the cloud shifts left and the moon clears it.
        MiniCloud(d, canvas, fx - 1.0f, fy + 1.0f, fs - 1.0f, false);
        DrawCrescent(d, canvas, fx + fs, fy - fs + 1.0f, fs - 1.0f, fs / 2.0f,
                     -fs / 2.0f, moon, moon);
        break;
      }
      FillDisc(d, canvas,
               Disc{fx + fs / 2.0f, fy - fs / 2.0f + 1.0f, fs / 2.0f + 1.0f},
               sun);
      MiniCloud(d, canvas, fx, fy + 1.0f, fs - 1.0f, false);
      break;
    case WeatherCondition::CLOUDY:
      MiniCloud(d, canvas, fx, fy, fs, false);
      break;
    case WeatherCondition::FOG:
      // Alternating inset, taken off both ends, so each bar stays centred on
      // the icon's column instead of the whole bank leaning one way.
      for (int i = 0; i < 3; i++) {
        const int inset = i % 2;
        d.horizontal_line(cx - s + inset, cy - s / 2 + i * 2,
                          2 * s + 1 - 2 * inset, fog);
      }
      break;
    case WeatherCondition::DRIZZLE:
      MiniCloud(d, canvas, fx, fy - 1.0f, fs - 1.0f, false);
      d.draw_pixel_at(cx - 2, cy + s - 1, rain);
      d.draw_pixel_at(cx + 2, cy + s - 1, rain);
      break;
    case WeatherCondition::FREEZING_DRIZZLE:
      MiniCloud(d, canvas, fx, fy - 1.0f, fs - 1.0f, false);
      d.draw_pixel_at(cx - 2, cy + s - 1, rain);
      d.draw_pixel_at(cx + 2, cy + s - 1, Color(210, 225, 245));
      break;
    case WeatherCondition::RAINY:
      MiniCloud(d, canvas, fx, fy - 1.0f, fs - 1.0f, false);
      d.vertical_line(cx - 2, cy + s - 1, 2, rain);
      d.vertical_line(cx + 2, cy + s - 1, 2, rain);
      break;
    case WeatherCondition::POURING:
      MiniCloud(d, canvas, fx, fy - 1.0f, fs - 1.0f, true);
      d.vertical_line(cx - 3, cy + s - 1, 2, rain);
      d.vertical_line(cx, cy + s - 1, 3, rain);
      d.vertical_line(cx + 3, cy + s - 1, 2, rain);
      break;
    case WeatherCondition::FREEZING_RAIN:
      MiniCloud(d, canvas, fx, fy - 1.0f, fs - 1.0f, false);
      d.vertical_line(cx - 2, cy + s - 1, 2, rain);
      d.draw_pixel_at(cx + 2, cy + s - 1, Color(210, 225, 245));
      break;
    case WeatherCondition::SNOWY: {
      // A small crystal (not a cloud), matching the hero.
      const Color sf(230, 244, 255);
      d.vertical_line(cx, cy - s + 1, 2 * s - 1, sf);
      d.horizontal_line(cx - s + 1, cy, 2 * s - 1, sf);
      d.draw_pixel_at(cx - 2, cy - 2, sf);
      d.draw_pixel_at(cx + 2, cy - 2, sf);
      d.draw_pixel_at(cx - 2, cy + 2, sf);
      d.draw_pixel_at(cx + 2, cy + 2, sf);
      break;
    }
    case WeatherCondition::SNOW_GRAINS:
      MiniCloud(d, canvas, fx, fy - 1.0f, fs - 1.0f, false);
      d.draw_pixel_at(cx - 2, cy + s - 1, snow);
      d.draw_pixel_at(cx + 2, cy + s - 1, snow);
      break;
    case WeatherCondition::THUNDERSTORM:
      // The bolt occupies the two columns left of centre, so its companion
      // streak sits the same distance right of centre and the pair balances.
      MiniCloud(d, canvas, fx, fy - 1.0f, fs - 1.0f, true);
      MiniBolt(d, cx - 2, cy + s - 5, bolt);
      d.vertical_line(cx + 2, cy + s - 3, 2, rain);
      break;
    case WeatherCondition::HAIL_THUNDERSTORM:
      MiniCloud(d, canvas, fx, fy - 1.0f, fs - 1.0f, true);
      MiniBolt(d, cx - 2, cy + s - 5, bolt);
      d.draw_pixel_at(cx + 2, cy + s - 3, Color(210, 225, 245));
      break;
    case WeatherCondition::UNKNOWN:
    default:
      MiniCloud(d, canvas, fx, fy, fs, false);
      break;
  }
}

}  // namespace esphome::pixoo64::weather
