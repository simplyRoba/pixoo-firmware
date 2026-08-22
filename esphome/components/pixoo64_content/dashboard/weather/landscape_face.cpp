#include "landscape_face.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdio>

#include "esphome/components/pixoo64_content/aa_draw.h"
#include "esphome/components/pixoo64_content/blend_canvas.h"

#include "pixoo_cmd.h"

namespace esphome::pixoo64::dashboard::weather {
namespace {

constexpr float kPi = 3.14159265358979323846f;

const Color kSunCore(255, 246, 210);
const Color kSunEdge(255, 176, 64);
const Color kSunGlow(255, 168, 72);
const Color kSunLight(255, 236, 180);
const Color kNight(6, 8, 20);
const Color kMoonLit(232, 235, 242);
const Color kMoonMare(150, 158, 186);
const Color kMoonRim(255, 255, 255);
const Color kMoonLimb(176, 182, 204);
const Color kMoonDark(22, 26, 40);
const Color kMoonlight(150, 165, 205);
const Color kText(240, 243, 250);

// Sun and moon are the same visual size. The sun follows the existing
// sunrise-to-sunset presentation arc. The moon uses its local rise-to-set
// progress horizontally and its topocentric altitude vertically.
constexpr float kSunR = 12.0f;
constexpr float kSunGlowR = 5.0f;
constexpr float kSunBaseY = 30.0f;   // sun height at the horizon edges
constexpr float kSunRise = 14.0f;    // how much higher the sun sits at midday
constexpr float kSunSink = 18.0f;    // extra drop as it fades through twilight
// At 0.05, glare reaches full strength within the final 90 seconds of the
// 30-minute twilight ramp. This keeps the visibly drawn twilight disc free of
// stars while the linear ramp still releases them smoothly as it disappears.
constexpr float kSunGlareFullDayness = 0.05f;
constexpr float kSunEdgeL = 6.0f;
constexpr float kSunEdgeR = 58.0f;

constexpr float kMoonR = 12.0f;
constexpr float kMoonBaseY = 30.0f;  // centre at the mathematical horizon
constexpr float kMoonRise = 14.0f;   // bounded height at the zenith
constexpr float kMoonHorizonFadeDegrees = 6.0f;
constexpr float kMoonDayAlpha = 0.30f;
// Height moonlight falls from. It sits far above the drawn disc so the night
// scene is lit from overhead rather than strongly side-lit from the disc.
constexpr float kMoonLightY = -34.0f;
// Full-moon light stays under daylight's 1.0 so night is the softer light.
// Unlit nights retain depth through the separate ambient term.
constexpr float kMoonStrengthPeak = 0.62f;

using content::BlendAt;
using content::Clamp01;
using content::DrawParticleSprite;
using content::Lerp;
using content::ParticlePixel;
using content::Blend;

// Background color at a pixel, computed analytically so anti-aliased shapes can
// blend their edges against the exact scene behind them (the panel offers no
// read-back). The sky is a vertical day gradient cross-faded toward night by
// dayness, so dawn and dusk pass through a warm, darkening sky rather than
// switching in one frame.
struct Scene {
  float dayness{0.0f};
  Color sky_top{kNight};
  Color sky_low{kNight};

  Color At(int y) const {
    const float t = static_cast<float>(y) / (pixoo::kHeight - 1);
    const Color day = Blend(this->sky_top, this->sky_low, t);
    return Blend(kNight, day, this->dayness);
  }
};

Scene MakeScene(const pixoo::SkyState &sky) {
  Scene s;
  s.dayness = sky.valid ? Clamp01(sky.dayness) : 0.0f;
  // The day gradient warms toward the horizon when the sun is low; morning
  // warms to orange, evening to a deeper red, so dawn and dusk differ.
  const float lowness = std::fabs(sky.day_fraction - 0.5f) * 2.0f;
  const bool morning = sky.day_fraction < 0.5f;
  const Color warm_top = morning ? Color(60, 46, 96) : Color(70, 30, 70);
  const Color warm_low = morning ? Color(240, 150, 74) : Color(224, 84, 62);
  s.sky_top = Blend(Color(18, 58, 120), warm_top, lowness);
  s.sky_low = Blend(Color(126, 186, 214), warm_low, lowness);
  return s;
}

void FillSky(display::Display &d, const Scene &scene) {
  for (int y = 0; y < pixoo::kHeight; y++) {
    const Color row = scene.At(y);
    for (int x = 0; x < pixoo::kWidth; x++)
      d.draw_pixel_at(x, y, row);
  }
}

// Integer hash: scatters the star index so positions look random rather than
// falling on a lattice.
uint32_t Hash(uint32_t x) { return pixoo::WeatherHash(x); }

// Fraction of the sun's glare reaching a pixel: full inside the disc, easing
// out across the glow on the same radial profile the glow is drawn with. The
// glare saturates well before the sun reaches full opacity, because even a
// half-faded twilight sun outshines a star, but it still returns to zero as
// the sun leaves the sky so no star-free hole is left behind.
float SunGlare(int x, int y, float sun_x, float sun_y, float dayness) {
  if (dayness <= 0.0f)
    return 0.0f;
  const float reach = kSunR + kSunGlowR;
  const float dx = x + 0.5f - sun_x;
  const float dy = y + 0.5f - sun_y;
  const float g = Clamp01((reach - std::sqrt(dx * dx + dy * dy)) / kSunGlowR);
  return g * g * Clamp01(dayness / kSunGlareFullDayness);
}

// Stars fade in as night falls (alpha = 1 - dayness). They blend over the
// current sky so they emerge through dusk rather than popping in, and the
// sun's glare washes out the ones close to it so none show through the disc
// while it is still translucent.
void DrawStars(display::Display &d, const Scene &scene, float alpha,
               uint64_t elapsed_ms, float sun_x, float sun_y, float dayness) {
  if (alpha <= 0.0f)
    return;
  for (int i = 0; i < 70; i++) {
    const uint32_t h = Hash(static_cast<uint32_t>(i));
    const int xi = static_cast<int>(h % pixoo::kWidth);
    const int yi = static_cast<int>((h >> 8) % pixoo::kHeight);
    const uint32_t period_ms = 1024u << (h & 1u);
    const float phase = static_cast<float>((elapsed_ms + (h >> 11)) % period_ms) /
                        static_cast<float>(period_ms);
    const float twinkle = 0.72f + 0.28f *
                                        (1.0f - std::fabs(phase * 2.0f - 1.0f));
    const float brightness =
        (0.30f + ((h >> 16) & 0xff) / 255.0f * 0.70f) * alpha * twinkle;
    const auto draw_star_pixel = [&](int x, int y, float pixel_brightness) {
      if (pixel_brightness <= 0.0f || x < 0 || x >= pixoo::kWidth || y < 0 ||
          y >= pixoo::kHeight)
        return;
      const float b = pixel_brightness *
                      (1.0f - SunGlare(x, y, sun_x, sun_y, dayness));
      if (b > 0.0f)
        d.draw_pixel_at(x, y, Blend(scene.At(y), Color(210, 216, 235), b));
    };
    draw_star_pixel(xi, yi, brightness);
    if (((h >> 24) & 0xff) > 210) {
      const float arm_brightness = brightness * 0.4f;
      draw_star_pixel(xi - 1, yi, arm_brightness);
      draw_star_pixel(xi + 1, yi, arm_brightness);
      draw_star_pixel(xi, yi - 1, arm_brightness);
      draw_star_pixel(xi, yi + 1, arm_brightness);
    }
  }
}

// Anti-aliased sun disc with a soft glow, faded by `alpha` so it emerges and
// fades through twilight. Both the glow and the disc composite over what is
// already in the frame, so a Moon or star sharing the sun's neighbourhood
// shows through the translucent glow instead of being replaced by bare sky.
void DrawSun(display::Display &d, const Scene &scene, float cx, float cy,
             float alpha, float glow_pulse) {
  if (alpha <= 0.0f)
    return;
  content::BlendCanvas *canvas = content::BlendCanvasOf(d);
  const float reach = kSunR + kSunGlowR;
  const int x0 = static_cast<int>(std::floor(cx - reach));
  const int x1 = static_cast<int>(std::ceil(cx + reach));
  const int y0 = static_cast<int>(std::floor(cy - reach));
  const int y1 = static_cast<int>(std::ceil(cy + reach));
  const float reach_sq = reach * reach;
  for (int y = y0; y <= y1; y++) {
    if (y < 0 || y >= pixoo::kHeight)
      continue;
    // The sky behind the disc and the vertical distance term are constant
    // along a row.
    const Color row_sky = scene.At(y);
    const float dy = y + 0.5f - cy;
    const float dy_sq = dy * dy;
    if (dy_sq >= reach_sq)
      continue;
    for (int x = x0; x <= x1; x++) {
      if (x < 0 || x >= pixoo::kWidth)
        continue;
      // Comparing squared distances rejects the bounding-box corners before
      // paying for a square root.
      const float dx = x + 0.5f - cx;
      const float dist_sq = dx * dx + dy_sq;
      if (dist_sq >= reach_sq)
        continue;
      const float dist = std::sqrt(dist_sq);
      const float g = Clamp01((reach - dist) / kSunGlowR);
      const float glow_a = g * g * 0.55f * glow_pulse * alpha;
      const float core_a = Clamp01(kSunR - dist + 0.5f) * alpha;
      if (glow_a <= 0.0f && core_a <= 0.0f)
        continue;
      const Color face = Blend(kSunCore, kSunEdge, Clamp01(dist / kSunR));
      if (canvas != nullptr) {
        if (glow_a > 0.0f)
          canvas->BlendPixel(x, y, kSunGlow, glow_a);
        if (core_a > 0.0f)
          canvas->BlendPixel(x, y, face, core_a);
        continue;
      }
      Color out = Blend(row_sky, kSunGlow, glow_a);
      out = Blend(out, face, core_a);
      d.draw_pixel_at(x, y, out);
    }
  }
}

// The sun's glow breathes on a slow triangle, scaling the glow term around 1.
float SunGlowPulse(uint64_t elapsed_ms) {
  constexpr uint32_t kPeriodMs = 4096u;
  const float phase = static_cast<float>(elapsed_ms % kPeriodMs) /
                      static_cast<float>(kPeriodMs);
  const float triangle = 1.0f - std::fabs(phase * 2.0f - 1.0f);
  return 1.08f - triangle * 0.16f;
}

// Near-side surface markings in disc coordinates: the maria as dark patches
// and the bright ray craters as small rimmed spots, positioned as the moon is
// seen from the northern hemisphere. Offsets and radii are fractions of the
// moon radius; `depth` is the darkening at the centre and `rim` the brightness
// of the ring around it.
struct MoonMark {
  float x;
  float y;
  float r;
  float depth;
  float rim;
};

constexpr MoonMark kMoonMarks[] = {
    {-0.36f, -0.36f, 0.36f, 0.52f, 0.0f},  // Imbrium
    {0.06f, -0.34f, 0.24f, 0.58f, 0.0f},   // Serenitatis
    {0.30f, -0.10f, 0.28f, 0.55f, 0.0f},   // Tranquillitatis
    {0.62f, -0.30f, 0.15f, 0.62f, 0.0f},   // Crisium
    {0.46f, 0.18f, 0.20f, 0.48f, 0.0f},    // Fecunditatis
    {-0.58f, 0.06f, 0.34f, 0.34f, 0.0f},   // Procellarum
    {-0.26f, 0.36f, 0.22f, 0.40f, 0.0f},   // Nubium
    {-0.10f, 0.62f, 0.13f, 0.20f, 0.75f},  // Tycho
    {-0.24f, 0.02f, 0.10f, 0.18f, 0.55f},  // Copernicus
};

// Anti-aliased moon disc with a soft phase terminator, faded by `alpha`. The
// terminator crosses each row at x = f*halfwidth (f runs -1 new .. +1 full).
void DrawMoon(display::Display &d, const Scene &scene, float mcx, float mcy,
              float illumination, bool waxing, float alpha, float dayness) {
  if (alpha <= 0.0f)
    return;
  content::BlendCanvas *canvas = content::BlendCanvasOf(d);
  const float r = kMoonR;
  const float f = 2.0f * illumination - 1.0f;
  const int y0 = static_cast<int>(std::floor(mcy - r - 1.0f));
  const int y1 = static_cast<int>(std::ceil(mcy + r + 1.0f));
  const float r_sq = r * r;
  const float reach = r + 0.5f;
  const float reach_sq = reach * reach;
  for (int y = y0; y <= y1; y++) {
    if (y < 0 || y >= pixoo::kHeight)
      continue;
    const float dy = y + 0.5f - mcy;
    const float dy_sq = dy * dy;
    if (dy_sq >= reach_sq)
      continue;
    // The terminator half-width is constant along a row, and solving the disc
    // for x bounds the row to the columns it can cover instead of scanning the
    // full bounding box.
    const float halfw = dy_sq < r_sq ? std::sqrt(r_sq - dy_sq) : 0.0f;
    const float boundary = f * halfw;
    const float row_half = std::sqrt(reach_sq - dy_sq);
    int x_from = static_cast<int>(std::ceil(mcx - row_half - 0.5f));
    int x_to = static_cast<int>(std::floor(mcx + row_half - 0.5f));
    if (x_from < 0)
      x_from = 0;
    if (x_to >= pixoo::kWidth)
      x_to = pixoo::kWidth - 1;
    for (int x = x_from; x <= x_to; x++) {
      const float dx = x + 0.5f - mcx;
      const float dist = std::sqrt(dx * dx + dy_sq);
      const float disc = Clamp01(r - dist + 0.5f);
      if (disc <= 0.0f)
        continue;
      const float lit = waxing ? Clamp01(dx + boundary + 0.5f)
                               : Clamp01(boundary - dx + 0.5f);
      // Surface: maria darken the plains, crater rims catch the light, and the
      // limb falls off so the disc reads as a sphere rather than a flat cutout.
      float mare = 0.0f;
      float rim = 0.0f;
      for (const MoonMark &m : kMoonMarks) {
        const float mx = dx - m.x * r;
        const float my = dy - m.y * r;
        const float mr = m.r * r;
        const float t2 = (mx * mx + my * my) / (mr * mr);
        if (t2 >= 1.0f)
          continue;
        mare = std::max(mare, m.depth * (1.0f - t2));
        if (m.rim > 0.0f)
          rim = std::max(rim, m.rim * Clamp01(1.0f - std::fabs(t2 - 0.66f) /
                                                         0.30f));
      }
      Color surface = Blend(kMoonLit, kMoonMare, mare);
      surface = Blend(surface, kMoonRim, rim);
      const float limb = dist / r;
      surface = Blend(surface, kMoonLimb, 0.30f * limb * limb * limb);
      const Color moon = Blend(kMoonDark, surface, lit);
      // Earthshine keeps the unlit disc readable at night. In daylight only
      // the illuminated portion remains visible against the bright sky.
      const float phase_visibility = Lerp(1.0f - dayness, 1.0f, lit);
      const float coverage = disc * alpha * phase_visibility;
      if (canvas != nullptr)
        canvas->BlendPixel(x, y, moon, coverage);
      else
        d.draw_pixel_at(x, y, Blend(scene.At(y), moon, coverage));
    }
  }
}

// Cloud layers repeat horizontally, so their fixed silhouettes can cross the
// scene indefinitely without stored particles or allocation. They are drawn
// after the celestial bodies, making them part of the sky rather than icons.
struct CloudPuff {
  int x;
  int y;
  int rx;
  int ry;
};

// A band's geometry is fixed for every cover level, so rising or falling cover
// changes only how present and how dark a band is. Interpolating spacing or
// height instead would slide every cloud sideways as the weather changed.
struct CloudLayer {
  int y;
  int spacing;
  uint32_t period_ms;
  uint32_t phase_ms;
  float cover_in;    // cover level at which the band starts appearing
  float tone_floor;  // darkness the band never goes below
};

// Cloud silhouettes. A layer cycles through these so consecutive clouds in a
// band differ instead of one shape repeating across the sky.
constexpr CloudPuff kCloudPuffsWide[] = {
    {-7, 8, 7, 4}, {1, 4, 9, 7}, {11, 6, 10, 5},
    {20, 9, 10, 3}, {7, 10, 23, 4},
};
constexpr CloudPuff kCloudPuffsHeaped[] = {
    {-4, 9, 6, 4}, {4, 3, 8, 7}, {13, 6, 9, 5}, {6, 10, 17, 4},
};
constexpr CloudPuff kCloudPuffsLong[] = {
    {-9, 9, 8, 3}, {2, 6, 11, 5}, {14, 5, 9, 6},
    {24, 8, 8, 4}, {6, 11, 24, 3},
};
constexpr CloudPuff kCloudPuffsSmall[] = {
    {-3, 8, 5, 4}, {3, 4, 7, 6}, {10, 7, 8, 4}, {4, 10, 13, 3},
};

struct CloudShape {
  const CloudPuff *puffs;
  uint8_t count;
};

constexpr CloudShape kCloudShapes[] = {
    {kCloudPuffsWide, 5u},
    {kCloudPuffsHeaped, 4u},
    {kCloudPuffsLong, 5u},
    {kCloudPuffsSmall, 4u},
};
constexpr uint8_t kCloudShapeCount = 4u;

// Cover level 1 is one high band, 2 adds a lower band, 3 fills the gap between
// them. Each band arrives over the level below it, so cover reads as a single
// quantity from clear to overcast.
constexpr CloudLayer kCloudBands[] = {
    {6, 62, 32768u, 0u, 0.0f, 0.0f},
    {19, 64, 65536u, 9000u, 1.0f, 1.0f},
    {11, 58, 49152u, 21000u, 2.0f, 1.0f},
};
constexpr uint8_t kCloudBandCount = 3u;

// How present a band is at a cover level: it feathers in over the one level
// below its own threshold.
float CloudBandOpacity(const CloudLayer &layer, float cover) {
  return Clamp01(cover - layer.cover_in);
}

// Cover also darkens the sky: a lone fair-weather cloud is white, an overcast
// deck is grey. Tone runs 0 bright to 2 dark.
float CloudBandTone(const CloudLayer &layer, float cover) {
  const float tone = Clamp01((cover - 1.0f) * 0.5f) * 2.0f;
  return std::max(tone, std::min(layer.tone_floor, tone + 1.0f));
}

// A cloud is lit from above: sunlit crowns, shadowed bases. A layer resolves
// its two endpoint colors once, then every pixel is one blend between them by
// its depth through the body, so the cloud reads as volume rather than a flat
// cut-out. The tone selects how dark the whole cloud sits, on a continuous
// scale so thickening cover darkens without a step.
struct CloudPalette {
  Color top;
  Color base;

  Color At(float shade) const {
    return Blend(this->top, this->base, Clamp01(shade));
  }
};

// One stop of the three-stop tone ramp, interpolated at a fractional tone.
Color CloudToneColor(const Color (&stops)[3], float tone) {
  const float t = tone < 0.0f ? 0.0f : (tone > 2.0f ? 2.0f : tone);
  const int low = static_cast<int>(t);
  const int high = low >= 2 ? 2 : low + 1;
  return Blend(stops[low], stops[high], t - low);
}

CloudPalette CloudPaletteFor(const Scene &scene, float tone,
                             float lightning_intensity) {
  constexpr Color kDayTop[3] = {Color(250, 252, 253), Color(226, 233, 240),
                                Color(186, 198, 212)};
  constexpr Color kDayBase[3] = {Color(186, 199, 214), Color(142, 157, 176),
                                 Color(98, 111, 131)};
  constexpr Color kNightTop[3] = {Color(96, 110, 134), Color(72, 86, 108),
                                  Color(52, 64, 84)};
  constexpr Color kNightBase[3] = {Color(48, 58, 76), Color(34, 43, 60),
                                   Color(23, 30, 44)};
  const Color day_top = CloudToneColor(kDayTop, tone);
  const Color day_base = CloudToneColor(kDayBase, tone);
  const Color night_top = CloudToneColor(kNightTop, tone);
  const Color night_base = CloudToneColor(kNightBase, tone);
  const float flash =
      lightning_intensity / pixoo::kLightningMaxIntensity * 0.32f;
  const Color lit(190, 210, 245);
  return {Blend(Blend(night_top, day_top, scene.dayness), lit, flash),
          Blend(Blend(night_base, day_base, scene.dayness), lit, flash)};
}

void DrawCloudLayer(display::Display &d, const Scene &scene,
                    const CloudLayer &layer, uint64_t elapsed_ms,
                    float lightning_intensity, float tone, float opacity) {
  if (opacity <= 0.0f)
    return;
  const uint64_t elapsed = elapsed_ms + layer.phase_ms;
  const uint64_t phase = elapsed % layer.period_ms;
  const float scroll = static_cast<float>(phase) * layer.spacing /
                       layer.period_ms;
  // Each period the band scrolls exactly one spacing, so a cloud's identity is
  // its slot within the band offset by the number of completed periods. Naming
  // it by slot alone would re-label every cloud at the wrap and visibly reshuffle
  // the silhouettes.
  const int64_t cycle =
      static_cast<int64_t>(elapsed / layer.period_ms);
  const int first = static_cast<int>(
                        std::floor((scroll - pixoo::kWidth) / layer.spacing)) -
                    1;
  const int last = static_cast<int>(
                       std::ceil((scroll + pixoo::kWidth) / layer.spacing)) +
                   1;
  const CloudPalette palette = CloudPaletteFor(scene, tone, lightning_intensity);
  // With read-back the rim can be feathered against whatever is behind it.
  content::BlendCanvas *canvas = content::BlendCanvasOf(d);
  for (int index = first; index <= last; index++) {
    const float origin = scroll - index * layer.spacing;
    const uint32_t shape_hash = Hash(static_cast<uint32_t>(
        static_cast<int64_t>(layer.phase_ms) + (index + cycle) * 0x9e37));
    const CloudShape &shape = kCloudShapes[shape_hash % kCloudShapeCount];

    // Vertical extent of this whole body, so shading spans the cloud rather
    // than restarting inside every puff. Horizontal bounds include the
    // feathered ellipse edge, keeping a fractional edge cloud from culling.
    int body_top = pixoo::kHeight;
    int body_bottom = 0;
    float body_left = static_cast<float>(pixoo::kWidth);
    float body_right = -static_cast<float>(pixoo::kWidth);
    for (uint8_t p = 0; p < shape.count; p++) {
      const CloudPuff &puff = shape.puffs[p];
      const float min_r = std::min(puff.rx, puff.ry);
      const float outer = 1.0f + 0.5f / min_r;
      body_top = std::min(body_top, layer.y + puff.y - puff.ry);
      body_bottom = std::max(body_bottom, layer.y + puff.y + puff.ry);
      body_left = std::min(body_left, puff.x - puff.rx * outer);
      body_right = std::max(body_right, puff.x + puff.rx * outer);
    }
    if (origin + body_right < -0.5f ||
        origin + body_left > pixoo::kWidth - 0.5f)
      continue;
    const float body_span =
        static_cast<float>(std::max(1, body_bottom - body_top));

    for (uint8_t p = 0; p < shape.count; p++) {
      const CloudPuff &puff = shape.puffs[p];
      const float cx = origin + puff.x + 0.5f;
      const float cy = layer.y + puff.y + 0.5f;
      const float rx = puff.rx;
      const float ry = puff.ry;
      const float inv_rx2 = 1.0f / (rx * rx);
      const float inv_ry2 = 1.0f / (ry * ry);
      const float min_r = std::min(rx, ry);
      // Radii bracketing the feathered rim: inside is solid, outside is clear.
      const float inner = 1.0f - 0.5f / min_r;
      const float outer = 1.0f + 0.5f / min_r;
      const float inner_sq = inner * inner;
      const float outer_sq = outer * outer;
      const float outer_ry = ry * outer;
      for (int y =
               static_cast<int>(std::floor(cy - outer_ry - 0.5f)) + 1;
           y <= static_cast<int>(std::ceil(cy + outer_ry - 0.5f)) - 1; y++) {
        if (y < 0 || y >= pixoo::kHeight)
          continue;
        // Shading is constant along a row: depth through the body, plus a
        // little of the puff's own curvature so individual lobes stay legible
        // against their neighbours.
        const float dy = y + 0.5f - cy;
        const float body_t = (y - body_top) / body_span;
        const float puff_t = 0.5f + 0.5f * dy / ry;
        const Color color = palette.At(0.68f * body_t + 0.32f * puff_t);
        // The vertical term of the normalised radius is constant along a row,
        // and solving the ellipse for x bounds the row to the columns it can
        // cover instead of scanning the full bounding box.
        const float dy_term = dy * dy * inv_ry2;
        const float span_sq = outer_sq - dy_term;
        if (span_sq <= 0.0f)
          continue;
        const float half_span = rx * std::sqrt(span_sq);
        int x_from = static_cast<int>(std::ceil(cx - half_span - 0.5f));
        int x_to = static_cast<int>(std::floor(cx + half_span - 0.5f));
        if (x_from < 0)
          x_from = 0;
        if (x_to >= pixoo::kWidth)
          x_to = pixoo::kWidth - 1;
        for (int x = x_from; x <= x_to; x++) {
          const float dx = x + 0.5f - cx;
          // Squared normalised radius: 1 exactly on the ellipse. Only the rim
          // needs a distance, so the interior and the clear outside skip the
          // square root entirely.
          const float norm_sq = dx * dx * inv_rx2 + dy_term;
          if (norm_sq >= outer_sq)
            continue;
          float coverage = 1.0f;
          if (norm_sq > inner_sq) {
            // Signed distance to the edge in pixels, so the rim feathers
            // instead of stepping straight from cloud to sky.
            const float edge = (1.0f - std::sqrt(norm_sq)) * min_r;
            coverage = Clamp01(edge + 0.5f);
            if (coverage <= 0.0f)
              continue;
          }
          if (canvas != nullptr)
            canvas->BlendPixel(x, y, color, coverage * opacity);
          else if (coverage * opacity > 0.5f)
            d.draw_pixel_at(x, y, color);
        }
      }
    }
  }
}

void DrawClouds(display::Display &d, const Scene &scene, float cover,
                uint64_t elapsed_ms, float lightning_intensity) {
  for (uint8_t i = 0; i < kCloudBandCount; i++)
    DrawCloudLayer(d, scene, kCloudBands[i], elapsed_ms, lightning_intensity,
                   CloudBandTone(kCloudBands[i], cover),
                   CloudBandOpacity(kCloudBands[i], cover));
}

uint8_t ScaleByte(uint8_t c, float f) {
  const float v = c * f;
  return static_cast<uint8_t>(v > 255.0f ? 255.0f : v);
}

// Lightning stays local to the cloud/sky band. It brightens the scene only a
// little; the bolt itself carries the readable peak rather than a white flash.
// Horizontal origin of a strike. The sky glow and the bolt share it, so the
// brightened region stays centred on the bolt.
int LightningOriginX(const pixoo::LightningState &lightning) {
  return 18 + static_cast<int>(lightning.bolt_seed % 29u);
}

void DrawLightningSkyGlow(display::Display &d, const Scene &scene,
                           const pixoo::LightningState &lightning,
                           float presence) {
  if (lightning.intensity == 0u || presence <= 0.0f)
    return;
  content::BlendCanvas *canvas = content::BlendCanvasOf(d);
  const int cx = LightningOriginX(lightning);
  const float strength = static_cast<float>(lightning.intensity) /
                         pixoo::kLightningMaxIntensity * 0.30f * presence;
  if (strength <= 0.0f)
    return;
  // The flash lights the whole sky and everything under it, fading with
  // distance from the strike. It reaches the ground: cutting it off mid-frame
  // left a hard horizontal seam across the scene.
  const Color flash(170, 196, 245);
  // Below this the blend cannot change an 8-bit channel, so the pixel is left
  // alone rather than paying for a composite that rounds away to nothing.
  constexpr float kMinVisibleAlpha = 1.0f / 512.0f;
  for (int y = 0; y < pixoo::kHeight; y++) {
    const float dy = static_cast<float>(y - 16) / 78.0f;
    const float row_falloff = 1.0f - dy * dy;
    if (strength * row_falloff * row_falloff <= kMinVisibleAlpha)
      continue;
    for (int x = 0; x < pixoo::kWidth; x++) {
      const float dx = static_cast<float>(x - cx) / 58.0f;
      // Squared falloff, so the glow fades out gradually instead of ending on
      // a visible rim.
      const float falloff = Clamp01(row_falloff - dx * dx);
      const float alpha = strength * falloff * falloff;
      if (alpha <= kMinVisibleAlpha)
        continue;
      if (canvas != nullptr)
        canvas->BlendPixel(x, y, flash, alpha);
      else
        d.draw_pixel_at(x, y, Blend(scene.At(y), flash, alpha));
    }
  }
}

// One stroke of a bolt: a white-hot core along the path with a coloured glow
// bleeding sideways from it, so the channel reads as incandescent air rather
// than a drawn line. `width` is the half-width of the glow in pixels; `core`
// selects whether this stroke carries the hot centre, which branches do not.
void DrawBoltStroke(display::Display &d, content::BlendCanvas *canvas,
                    const Scene &scene, float x0, float y0, float x1, float y1,
                    float width, float alpha, bool core) {
  const Color kCore(255, 255, 255);
  const Color kGlow(188, 208, 255);
  const float span_x = x1 - x0;
  const float span_y = y1 - y0;
  const float length = std::sqrt(span_x * span_x + span_y * span_y);
  if (length <= 0.0f)
    return;
  const int min_x = static_cast<int>(std::floor(std::min(x0, x1) - width - 1));
  const int max_x = static_cast<int>(std::ceil(std::max(x0, x1) + width + 1));
  const int min_y = static_cast<int>(std::floor(std::min(y0, y1) - width - 1));
  const int max_y = static_cast<int>(std::ceil(std::max(y0, y1) + width + 1));
  for (int y = min_y; y <= max_y; y++) {
    if (y < 0 || y >= pixoo::kHeight)
      continue;
    for (int x = min_x; x <= max_x; x++) {
      if (x < 0 || x >= pixoo::kWidth)
        continue;
      // Distance from the pixel centre to the stroke, as a line segment.
      const float px = x + 0.5f - x0;
      const float py = y + 0.5f - y0;
      const float t =
          Clamp01((px * span_x + py * span_y) / (length * length));
      const float nx = px - span_x * t;
      const float ny = py - span_y * t;
      const float dist = std::sqrt(nx * nx + ny * ny);
      if (dist > width)
        continue;
      // Glow falls off with distance; the core is a narrow, hard centre.
      const float halo = Clamp01(1.0f - dist / width);
      const float glow_a = halo * halo * alpha;
      const float core_a = core ? Clamp01(1.0f - dist / 0.9f) * alpha : 0.0f;
      if (canvas != nullptr) {
        if (glow_a > 0.0f)
          canvas->BlendPixel(x, y, kGlow, glow_a * 0.85f);
        if (core_a > 0.0f)
          canvas->BlendPixel(x, y, kCore, core_a);
      } else if (core_a > 0.35f || glow_a > 0.55f) {
        d.draw_pixel_at(x, y, core_a > 0.35f ? kCore : kGlow);
      }
    }
  }
}

// A bolt is a jagged channel from cloud base toward the horizon: many short
// segments rather than a few long ones, tapering as it descends, with forks
// that peel off and die out. A single straight line at rain width was
// indistinguishable from the rain falling around it.
void DrawLightningBolt(display::Display &d, const Scene &scene,
                       const pixoo::LightningState &lightning,
                       float presence) {
  if (lightning.intensity == 0u || presence <= 0.0f)
    return;
  content::BlendCanvas *canvas = content::BlendCanvasOf(d);
  const float level = static_cast<float>(lightning.intensity) /
                      pixoo::kLightningMaxIntensity;
  const float alpha = Clamp01(0.45f + level * 0.55f) * presence;
  if (alpha <= 0.0f)
    return;

  const uint32_t seed = lightning.bolt_seed;
  constexpr int kSegments = 7;
  float x = static_cast<float>(LightningOriginX(lightning));
  float y = 8.0f + static_cast<float>((seed >> 5) % 6u);
  // Overall lean, so a bolt slants instead of always dropping straight down.
  const float lean = (static_cast<float>((seed >> 17) % 9u) - 4.0f) * 0.5f;

  for (int segment = 0; segment < kSegments; segment++) {
    const uint32_t h = pixoo::WeatherHash(seed + segment * 0x9e37u);
    const float jag = static_cast<float>(h % 11u) - 5.0f;
    const float next_x = x + jag * 0.62f + lean;
    const float next_y = y + 4.0f + static_cast<float>((h >> 7) % 3u);
    // The channel narrows toward the tip.
    const float t = static_cast<float>(segment) / (kSegments - 1);
    const float width = 2.5f - 1.35f * t;
    DrawBoltStroke(d, canvas, scene, x, y, next_x, next_y, width, alpha, true);

    // Forks peel away from a joint and fade out; they carry no hot core.
    if ((h & 3u) == 0u && segment > 0 && segment < kSegments - 1) {
      const float dir = (h & 4u) != 0u ? 1.0f : -1.0f;
      const float fork_x = x + dir * (3.0f + static_cast<float>((h >> 9) % 5u));
      const float fork_y = y + 3.0f + static_cast<float>((h >> 12) % 4u);
      DrawBoltStroke(d, canvas, scene, x, y, fork_x, fork_y, width * 0.7f,
                     alpha * 0.6f, false);
    }
    x = next_x;
    y = next_y;
  }
}

// The ground surface. The horizon is not a straight row: a shallow convex arc
// reads as the planet's curvature, and two offset low-frequency waves roll it
// into hills. Everything that stands on the ground or settles onto it reads
// this one function, so terrain, buildings, and weather cover stay aligned.
constexpr float kGroundCrestY = 57.4f;  // surface at the crest of the arc
constexpr float kGroundCurve = 2.7f;    // additional drop at the far edges
constexpr float kGroundHills = 1.35f;   // rolling-hill amplitude

// The profile is fixed, so it is evaluated once into a per-column table rather
// than recomputing transcendentals for every pixel, particle, and neighbour
// lookup in a frame.
struct GroundProfile {
  float surface[pixoo::kWidth];
  int16_t first_row[pixoo::kWidth];

  GroundProfile() {
    constexpr float half = pixoo::kWidth * 0.5f;
    for (int x = 0; x < pixoo::kWidth; x++) {
      const float t = (x + 0.5f - half) / half;
      const float hills = 0.62f * std::sin(x * 0.113f + 0.7f) +
                          0.38f * std::sin(x * 0.271f + 2.3f);
      this->surface[x] =
          kGroundCrestY + kGroundCurve * t * t + kGroundHills * hills;
      this->first_row[x] =
          static_cast<int16_t>(std::ceil(this->surface[x]));
    }
  }
};

const GroundProfile &Ground() {
  static const GroundProfile profile;
  return profile;
}

int ClampX(int x) {
  return x < 0 ? 0 : (x >= pixoo::kWidth ? pixoo::kWidth - 1 : x);
}

// Fractional surface row: the boundary between sky and earth, so the top of
// each column can be anti-aliased against the sky behind it.
float GroundSurfaceAt(int x) { return Ground().surface[ClampX(x)]; }

// First fully covered earth row of a column: what a solid object rests on.
int GroundYAt(int x) { return Ground().first_row[ClampX(x)]; }

// Buildings stand on the lowest row under their footprint, so uneven terrain
// never opens a gap beneath a wall.
int GroundYUnder(int x0, int x1) {
  int y = GroundYAt(x0);
  for (int x = x0 + 1; x <= x1; x++)
    y = std::max(y, GroundYAt(x));
  return y;
}

// Landscape furniture. Snow caps and ice highlights derive their rows from the
// same geometry, so they follow the building up and down the terrain.
constexpr int kHouseX0 = 50;
constexpr int kHouseX1 = 56;
constexpr int kHouseRoofRise = 10;  // peak height above the base row
// Wall and roof span the same number of rows, so the cottage reads as a
// building under a roof rather than a roof resting on a plinth.
constexpr int kHouseWallRows = 5;
constexpr int kTreeTrunkX = 41;
constexpr float kTreeCanopyRadius = 4.0f;

// Horizontal centre of the cottage, on the pixel grid.
constexpr float kHouseMidC = (kHouseX0 + kHouseX1) / 2 + 0.5f;
// Roof half-width at the eaves; the eaves overhang the walls on both sides.
constexpr float kRoofEaveHalf = (kHouseX1 - kHouseX0) / 2 + 2;

// Cottage placement over the fixed terrain: the row the walls stand on, the
// peak and eave rows, and the pitch of the roof edge. It is fixed, so it
// resolves once instead of walking the footprint again for every roof pixel
// and every flake that settles on one.
struct HouseGeometry {
  int base_y;
  int peak_y;
  int eave_y;
  float half_per_row;  // roof half-width gained per row down from the peak
  float across_edge;   // horizontal overshoot -> distance across the pitch

  HouseGeometry() {
    this->base_y = GroundYUnder(kHouseX0, kHouseX1);
    this->peak_y = this->base_y - kHouseRoofRise;
    // The eaves sit one row above the top wall row, so the roof rests on the
    // walls rather than covering their topmost course.
    this->eave_y = this->base_y - kHouseWallRows - 1;
    const int rows = this->eave_y - this->peak_y + 1;
    this->half_per_row =
        rows > 0 ? kRoofEaveHalf / static_cast<float>(rows) : kRoofEaveHalf;
    this->across_edge =
        1.0f / std::sqrt(1.0f + this->half_per_row * this->half_per_row);
  }
};

const HouseGeometry &House() {
  static const HouseGeometry geometry;
  return geometry;
}

int HouseBaseY() { return House().base_y; }
int HouseRoofPeakY() { return House().peak_y; }
int HouseEaveY() { return House().eave_y; }
float TreeCanopyCenterX() { return kTreeTrunkX + 0.5f; }
float TreeCanopyCenterY() { return GroundYAt(kTreeTrunkX) - 6.0f; }

// Half-width of the roof at row y: the triangle's edge grows linearly from the
// peak down to the eaves. The roof and anything settling on it share this, so
// a snow cap cannot overhang the tiles.
float RoofHalfWidthAt(int y) {
  const HouseGeometry &g = House();
  return static_cast<float>(y - g.peak_y + 1) * g.half_per_row + 0.5f;
}

// How much of a pixel the roof covers, from its distance out of the centre
// line and the half-width of its row. The pitch is a sloped edge, so the
// horizontal overshoot is measured across that edge rather than along the row;
// taken along the row, a pitch near one half-width per row falls exactly on
// pixel boundaries and the tiles step in whole pixels.
float RoofCoverage(float distance_from_mid, float half) {
  return Clamp01(0.5f - (distance_from_mid - half) * House().across_edge);
}

// How much of a pixel the round canopy covers, from its distance to the canopy
// centre.
float CanopyCoverage(float radius) {
  return Clamp01(kTreeCanopyRadius - radius + 0.5f);
}

// Widest reach of a partly covered canopy pixel.
constexpr float kCanopyReach = kTreeCanopyRadius + 0.5f;

// Material colors of the landscape, lit at render time.
const Color kMatGrassNear(78, 128, 62);  // brighter, warmer grass in front
const Color kMatGrassFar(34, 66, 44);    // cooler, darker grass at the horizon
const Color kMatCanopy(46, 92, 50);
const Color kMatTrunk(84, 60, 40);
const Color kMatWall(206, 184, 150);
// Fired-clay tiles, kept dark enough that the roof stays a silhouette against
// the warm low sky of dawn and dusk, which shares its hue.
const Color kMatRoof(124, 48, 40);
const Color kMatRoofEave(86, 32, 30);  // tiles deepen toward the eaves
const Color kMatDoor(96, 62, 40);
const Color kMatWindow(120, 138, 150);

// The horizon landscape: a band of earth with a house and a tree. Each surface
// has a material color, lit from a light source: an ambient floor plus a
// directional term for the side facing the light, tinted by the light color.
// The caller blends the light between the sun and moon by dayness, so the
// landscape hands over smoothly through twilight.
void DrawLandscape(display::Display &d, float lx, float ly, Color light,
                   float strength, float ambient) {
  // Curved and sloped silhouettes carry fractional edge pixels, which mix into
  // the frame already drawn behind them rather than replacing it.
  content::BlendCanvas *canvas = content::BlendCanvasOf(d);

  const auto shade = [&](Color mat, float px, float py, float nx,
                         float ny) -> Color {
    const float dx = lx - px;
    const float dy = ly - py;
    const float len = std::sqrt(dx * dx + dy * dy);
    const float ux = len > 0.0f ? dx / len : 0.0f;
    const float uy = len > 0.0f ? dy / len : 0.0f;
    const float facing = Clamp01(nx * ux + ny * uy);  // 0 away..1 toward light
    float amt = ambient + facing * strength * 0.7f;
    if (amt > 1.0f)
      amt = 1.0f;
    const Color base(ScaleByte(mat.r, amt), ScaleByte(mat.g, amt),
                     ScaleByte(mat.b, amt));
    return Blend(base, light, facing * strength * 0.25f);
  };

  // Earth band: a depth gradient from cool dark green at the surface to a
  // brighter warm green in front, with a little per-pixel texture. Depth runs
  // from each column's own surface, so the gradient follows the terrain
  // instead of banding straight across the hills. The surface normal tilts
  // with the local slope, so a hillside facing the light catches more of it.
  for (int x = 0; x < pixoo::kWidth; x++) {
    const float surface = GroundSurfaceAt(x);
    const int top = static_cast<int>(std::floor(surface));
    const float span = pixoo::kHeight - surface;
    const float slope =
        (GroundSurfaceAt(x + 1) - GroundSurfaceAt(x - 1)) * 0.5f;
    // Downhill-facing surface normal, normalized: (slope, -1) turned to unit.
    const float nlen = std::sqrt(slope * slope + 1.0f);
    const float nx = slope / nlen;
    const float ny = -1.0f / nlen;
    for (int y = top; y < pixoo::kHeight; y++) {
      if (y < 0)
        continue;
      const float depth =
          span > 1.0f ? Clamp01((y + 0.5f - surface) / (span - 1.0f)) : 1.0f;
      const Color row = Blend(kMatGrassFar, kMatGrassNear, depth);
      const uint32_t h = Hash(static_cast<uint32_t>(y) * 64u + x);
      const float jitter = 0.92f + (h & 0xff) / 255.0f * 0.16f;
      const Color mat(ScaleByte(row.r, jitter), ScaleByte(row.g, jitter),
                      ScaleByte(row.b, jitter));
      const Color lit = shade(mat, x + 0.5f, y + 0.5f, nx, ny);
      // The topmost row is partly sky: blend it by how much earth covers it.
      const float cov = Clamp01(y + 1.0f - surface);
      if (cov >= 1.0f)
        d.draw_pixel_at(x, y, lit);
      else
        BlendAt(d, canvas, x, y, lit, cov);
    }
  }

  // Tree: a trunk under a round canopy. The canopy is a sphere, so its normal
  // is the radial direction from the canopy center.
  const int trunk_x = kTreeTrunkX;
  const int trunk_base_y = GroundYAt(trunk_x);
  for (int y = trunk_base_y - 4; y < trunk_base_y; y++)
    d.draw_pixel_at(trunk_x, y,
                    shade(kMatTrunk, trunk_x + 0.5f, y + 0.5f, 0.0f, -1.0f));
  const float ccx = TreeCanopyCenterX();
  const float ccy = TreeCanopyCenterY();
  // The rim is anti-aliased against what is behind it, so the canopy reads as
  // round rather than as a stepped disc.
  for (int y = static_cast<int>(std::floor(ccy - kCanopyReach));
       y <= static_cast<int>(std::ceil(ccy + kCanopyReach)); y++)
    for (int x = static_cast<int>(std::floor(ccx - kCanopyReach));
         x <= static_cast<int>(std::ceil(ccx + kCanopyReach)); x++) {
      const float rx = x + 0.5f - ccx;
      const float ry = y + 0.5f - ccy;
      const float rsq = rx * rx + ry * ry;
      if (rsq > kCanopyReach * kCanopyReach)
        continue;
      const float rl = std::sqrt(rsq);
      const float nx = rl > 0.0f ? rx / rl : 0.0f;
      const float ny = rl > 0.0f ? ry / rl : 0.0f;
      BlendAt(d, canvas, x, y,
                          shade(kMatCanopy, x + 0.5f, y + 0.5f, nx, ny),
                          CanopyCoverage(rl));
    }

  // House: a European cottage. Walls under a pitched roof of the same height,
  // whose eaves overhang the walls on both sides.
  const int hx0 = kHouseX0;
  const int hx1 = kHouseX1;
  const int mid = (hx0 + hx1) / 2;
  const int house_base_y = HouseBaseY();
  const int body_top = house_base_y - kHouseWallRows;
  const int eave_y = HouseEaveY();
  const int roof_peak_y = HouseRoofPeakY();
  // Walls run down to each column's own surface, so the cottage meets sloped
  // ground without floating or leaving a gap.
  //
  // The wall normal turns continuously across the facade instead of flipping
  // at the centre. A single flipped normal gives every pixel on a side the
  // same value, so the cottage reads as two flat slabs meeting at a seam;
  // curving it makes the wall round away from the light like a real volume.
  // It also tilts upward, because sun and moon ride far above the walls and a
  // purely horizontal normal is nearly blind to them.
  const float wall_mid = (hx0 + hx1) * 0.5f + 0.5f;
  const float wall_half = (hx1 - hx0) * 0.5f + 0.5f;
  for (int x = hx0; x <= hx1; x++) {
    const float across = (x + 0.5f - wall_mid) / wall_half;  // -1 .. +1
    const float nx = 0.86f * across;
    const float ny = -std::sqrt(Clamp01(1.0f - nx * nx)) * 0.62f;
    for (int y = body_top; y < GroundYAt(x); y++)
      d.draw_pixel_at(x, y, shade(kMatWall, x + 0.5f, y + 0.5f, nx, ny));
  }

  // A door and a shuttered window, so the facade is a cottage rather than a
  // blank block. Both are recessed: they face straight out and catch no
  // direct light, which is what separates them from the wall around them.
  const int door_x = mid + 1;
  const int door_top = body_top + 1;
  for (int y = door_top; y < GroundYAt(door_x); y++)
    d.draw_pixel_at(door_x, y,
                    shade(kMatDoor, door_x + 0.5f, y + 0.5f, 0.0f, 0.0f));
  const int window_x = mid - 2;
  const int window_y = body_top + 1;
  d.draw_pixel_at(window_x, window_y,
                  shade(kMatWindow, window_x + 0.5f, window_y + 0.5f, 0.0f,
                        0.0f));
  // Roof: a straight-sided triangle whose half-width grows linearly with each
  // row down from the peak, so the rise is one clean sloped line; each edge
  // pixel is anti-aliased by its coverage of that boundary.
  const float mid_c = kHouseMidC;
  for (int y = roof_peak_y; y <= eave_y; y++) {
    const float half = RoofHalfWidthAt(y);
    const int x0 = static_cast<int>(std::floor(mid_c - half - 1.0f));
    const int x1 = static_cast<int>(std::ceil(mid_c + half + 1.0f));
    for (int x = x0; x <= x1; x++) {
      const float d_from_mid = std::fabs(x + 0.5f - mid_c);
      const float cov = RoofCoverage(d_from_mid, half);
      if (cov <= 0.0f)
        continue;
      // Each pitch is a flat plane, so its normal is constant across the
      // slope; a gentle ridge-to-eave falloff keeps the tiles from reading as
      // one poster-flat triangle.
      const float nx = x + 0.5f < mid_c ? -0.55f : 0.55f;
      const float along = half > 0.0f ? d_from_mid / half : 0.0f;
      const Color tile = Blend(kMatRoof, kMatRoofEave, Clamp01(along) * 0.5f);
      const Color lit = shade(tile, x + 0.5f, y + 0.5f, nx, -0.83f);
      BlendAt(d, canvas, x, y, lit, cov);
    }
  }
}

// Fog is a medium: the scene behind it attenuates toward the fog colour rather
// than being replaced. It is thickest along the ground where the sight line
// passes through the most air, thinning with height until the sky is clear.
// Rolling in and out is the same field at a lower opacity, so the bank keeps
// its shape and motion while it thickens or clears.
//
// This is the one effect that genuinely needs per-pixel transparency. Faking it
// by covering a fraction of pixels leaves visible grain at exactly the
// half-covered densities that dominate a fog bank, so it composites through
// BlendCanvas instead.

// Opacity profile with height. Row-constant, so it resolves once per scanline.
float FogProfileAt(int y) {
  constexpr float kTopY = 20.0f;    // highest wisps
  constexpr float kDenseY = 56.0f;  // deepest air
  const float h = Clamp01((y - kTopY) / (kDenseY - kTopY));
  return h * h * (3.0f - 2.0f * h);
}

// The bank stops short of total whiteout, so the landscape stays legible as a
// silhouette instead of vanishing outright.
constexpr float kFogMaxOpacity = 0.93f;

// The density field is smooth and low-frequency by construction, so it carries
// no detail at pixel scale. Sampling it on a coarse lattice once per frame and
// interpolating between those samples is visually equivalent to evaluating it
// everywhere, at a fraction of the cost: the noise is the single most
// expensive thing this face does.
constexpr int kFogCell = 4;
constexpr int kFogGridW = pixoo::kWidth / kFogCell + 2;
constexpr int kFogTopY = 18;
constexpr int kFogGridH = (pixoo::kHeight - kFogTopY) / kFogCell + 2;
// Cells needed to cover the width; each one interpolates between the lattice
// column at its left edge and the next.
constexpr int kFogCellsAcross = (pixoo::kWidth + kFogCell - 1) / kFogCell;
static_assert(kFogCellsAcross + 1 <= kFogGridW,
              "the last cell reads the lattice column past its right edge");

void DrawFog(display::Display &d, const Scene &scene, uint64_t elapsed_ms,
             float density) {
  if (density <= 0.0f)
    return;
  content::BlendCanvas *canvas = content::BlendCanvasOf(d);
  const Color day(214, 221, 226);
  const Color night(104, 118, 138);
  const Color base = Blend(night, day, scene.dayness);

  float grid[kFogGridH][kFogGridW];
  for (int gy = 0; gy < kFogGridH; gy++)
    for (int gx = 0; gx < kFogGridW; gx++)
      grid[gy][gx] = pixoo::FogDensityAt(
          static_cast<float>(gx * kFogCell),
          static_cast<float>(kFogTopY + gy * kFogCell), elapsed_ms,
          static_cast<uint32_t>(pixoo::kWidth));

  // Walking one lattice cell at a time lifts the cell's four corners and the
  // interpolation weights out of the per-pixel body. The sub-cell weights
  // repeat every cell, so they are a fixed table rather than a division.
  constexpr float kSubWeights[kFogCell] = {0.0f, 0.25f, 0.5f, 0.75f};
  static_assert(kFogCell == 4, "kSubWeights lists one weight per cell column");
  const float scale = kFogMaxOpacity * density;
  for (int y = kFogTopY; y < pixoo::kHeight; y++) {
    const float profile = FogProfileAt(y);
    if (profile <= 0.0f)
      continue;
    const int gy = (y - kFogTopY) / kFogCell;
    const float ty = kSubWeights[(y - kFogTopY) % kFogCell];
    const float *const upper = grid[gy];
    const float *const lower = grid[gy + 1];
    for (int gx = 0; gx < kFogCellsAcross; gx++) {
      const int x_begin = gx * kFogCell;
      const float top_left = upper[gx];
      const float top_span = upper[gx + 1] - top_left;
      const float bottom_left = lower[gx];
      const float bottom_span = lower[gx + 1] - bottom_left;
      const int x_end = std::min(x_begin + kFogCell, pixoo::kWidth);
      for (int x = x_begin; x < x_end; x++) {
        const float tx = kSubWeights[x - x_begin];
        const float top = top_left + top_span * tx;
        const float bottom = bottom_left + bottom_span * tx;
        const float grid_density = top + (bottom - top) * ty;
        // Density varies the opacity around the height profile, so the bank has
        // thick and thin patches drifting through it.
        const float opacity =
            Clamp01(profile * (0.35f + 0.85f * grid_density)) * scale;
        if (opacity <= 0.0f)
          continue;
        // Denser air also reads brighter, giving the bank internal depth rather
        // than one flat wash.
        const Color tone =
            Blend(base, day, Clamp01(grid_density - 0.4f) * 0.6f);
        if (canvas != nullptr)
          canvas->BlendPixel(x, y, tone, opacity);
        else if (opacity >= 0.5f)
          d.draw_pixel_at(x, y, tone);
      }
    }
  }
}

Color PrecipitationColor(const Scene &scene, bool freezing, bool heavy) {
  const Color day = freezing ? Color(190, 224, 242)
                             : heavy ? Color(84, 122, 159)
                                     : Color(116, 153, 184);
  const Color night = freezing ? Color(134, 178, 214)
                               : heavy ? Color(54, 78, 112)
                                       : Color(82, 112, 147);
  return Blend(night, day, scene.dayness);
}

struct ParticleClock {
  uint32_t phase;
  uint32_t cycle;
};

// How present one particle of a field that is arriving or retiring is. Each
// particle holds a fixed place in a hash order and crosses a narrow band of its
// own, so a field fills in or thins out drop by drop across the whole scene
// while every remaining drop keeps its normal path and brightness.
float ParticlePresence(uint32_t seed, float weight) {
  if (weight >= 1.0f)
    return 1.0f;
  if (weight <= 0.0f)
    return 0.0f;
  constexpr float kBand = 0.14f;
  const float order =
      static_cast<float>(Hash(seed ^ 0x9d2c5681u) & 0xffffu) / 65535.0f;
  return Clamp01((weight * (1.0f + kBand) - order) / kBand);
}

ParticleClock ParticleClockAt(uint64_t elapsed_ms, uint32_t period_ms,
                              uint32_t seed) {
  if (period_ms == 0u)
    return {0u, 0u};
  const uint32_t offset = Hash(seed ^ 0x68bc21ebu) % period_ms;
  const uint64_t particle_ms = elapsed_ms + offset;
  return {static_cast<uint32_t>(particle_ms % period_ms),
          static_cast<uint32_t>(particle_ms / period_ms)};
}

int ParticleX(uint32_t index, uint32_t count, uint32_t seed, uint32_t cycle,
              uint32_t phase, uint32_t period_ms, int drift_px) {
  if (count == 0u)
    return 0;
  // Wider lanes prevent persistent empty regions without placing every
  // particle on a visible grid. A particle picks a new position in its lane
  // only when it starts another fall.
  uint32_t lane_count = (count + 1u) / 2u;
  if (lane_count < 3u)
    lane_count = 3u;
  else if (lane_count > 8u)
    lane_count = 8u;
  const uint32_t lane = index % lane_count;
  const uint32_t lane_start = lane * pixoo::kWidth / lane_count;
  const uint32_t lane_end = (lane + 1u) * pixoo::kWidth / lane_count;
  const uint32_t lane_span = lane_end - lane_start;
  const uint32_t lane_width = lane_span == 0u ? 1u : lane_span;
  const uint32_t position = Hash(seed ^ (cycle * 0x9e3779b9u));
  const int base = static_cast<int>(lane_start + position % lane_width);
  const int drift =
      period_ms == 0u
          ? 0
          : static_cast<int>(phase * (2 * drift_px + 1) / period_ms) -
                drift_px;
  return (base + drift + pixoo::kWidth) % pixoo::kWidth;
}

int ParticleY(uint32_t phase, uint32_t period_ms, int top, int bottom) {
  if (period_ms == 0u || bottom <= top)
    return top;
  const uint32_t span = static_cast<uint32_t>(bottom - top + 1);
  return top + static_cast<int>(static_cast<uint64_t>(phase) * span /
                                period_ms);
}

constexpr ParticlePixel kSnowFarSprite[] = {{0, 0, 155u}};
constexpr ParticlePixel kSnowMidSprite[] = {
    {0, 0, 205u}, {1, 0, 85u}, {0, 1, 70u}};
constexpr ParticlePixel kSnowNearSprite[] = {
    {0, 0, 225u}, {-1, 0, 105u}, {1, 0, 105u},
    {0, -1, 95u}, {0, 1, 95u}};
constexpr ParticlePixel kSnowGrainSprite[] = {
    {0, 0, 180u}, {1, 0, 50u}};
constexpr ParticlePixel kHailSprite[] = {
    {0, 0, 225u}, {1, 0, 105u}, {0, 1, 90u},
    {-1, 0, 55u}, {0, -1, 65u}};

Color SnowColor(const Scene &scene, uint8_t depth) {
  const Color day = depth == 0u ? Color(174, 204, 220)
                    : depth == 1u ? Color(205, 226, 235)
                                 : Color(234, 242, 246);
  const Color night = depth == 0u ? Color(102, 137, 171)
                      : depth == 1u ? Color(136, 173, 202)
                                   : Color(174, 202, 222);
  return Blend(night, day, scene.dayness);
}

void DrawSnow(display::Display &d, const Scene &scene, uint64_t elapsed_ms,
              pixoo::PrecipitationIntensity intensity, float weight) {
  constexpr uint8_t kCount[] = {15u, 11u, 7u};
  constexpr int32_t kPeriodOffsetMs[] = {2800, 0, -2400};
  constexpr int kDriftOffset[] = {-1, 0, 1};
  content::BlendCanvas *canvas = content::BlendCanvasOf(d);
  const pixoo::PrecipitationKinematics kinematics =
      pixoo::PrecipitationKinematicsFor(pixoo::PrecipitationKind::SNOW,
                                        intensity);
  for (uint8_t depth = 0; depth < 3u; depth++) {
    const uint32_t period_ms = static_cast<uint32_t>(
        static_cast<int32_t>(kinematics.fall_period_ms) +
        kPeriodOffsetMs[depth]);
    const int drift_px = kinematics.drift_px + kDriftOffset[depth];
    const Color flake = SnowColor(scene, depth);
    for (uint8_t i = 0; i < kCount[depth]; i++) {
      const uint32_t seed = Hash(0x5a17u + depth * 0x9e37u + i * 0x45d9u);
      const ParticleClock clock = ParticleClockAt(elapsed_ms, period_ms, seed);
      const int x = ParticleX(i, kCount[depth], seed, clock.cycle,
                              clock.phase, period_ms, drift_px);
      const int bottom = GroundYAt(x) - (depth == 0u ? 1 : 2);
      const int y = ParticleY(clock.phase, period_ms, -2, bottom);
      const float fade = ParticlePresence(seed, weight);
      if (fade <= 0.0f)
        continue;
      if (depth == 0u) {
        DrawParticleSprite(d, canvas, x, y, flake, fade, kSnowFarSprite);
      } else if (depth == 1u) {
        DrawParticleSprite(d, canvas, x, y, flake, fade, kSnowMidSprite);
      } else {
        DrawParticleSprite(d, canvas, x, y, flake, fade, kSnowNearSprite);
      }
    }
  }
}

void DrawSnowGrains(display::Display &d, const Scene &scene,
                    uint64_t elapsed_ms,
                    pixoo::PrecipitationIntensity intensity, float weight) {
  constexpr uint8_t kCount = 76u;
  content::BlendCanvas *canvas = content::BlendCanvasOf(d);
  const pixoo::PrecipitationKinematics kinematics =
      pixoo::PrecipitationKinematicsFor(
          pixoo::PrecipitationKind::SNOW_GRAINS, intensity);
  const Color grain = SnowColor(scene, 0u);
  for (uint8_t i = 0; i < kCount; i++) {
    const uint32_t seed = Hash(0x3c6ef35fu + i * 0x27d4eb2du);
    const ParticleClock clock =
        ParticleClockAt(elapsed_ms, kinematics.fall_period_ms, seed);
    const int x = ParticleX(i, kCount, seed, clock.cycle, clock.phase,
                            kinematics.fall_period_ms, kinematics.drift_px);
    const int y = ParticleY(clock.phase, kinematics.fall_period_ms, -1,
                            GroundYAt(x) - 1);
    const float fade = ParticlePresence(seed, weight);
    if (fade > 0.0f)
      DrawParticleSprite(d, canvas, x, y, grain, fade, kSnowGrainSprite);
  }
}

void DrawHail(display::Display &d, const Scene &scene, uint64_t elapsed_ms,
              pixoo::PrecipitationIntensity intensity, float weight) {
  constexpr uint8_t kCount = 42u;
  constexpr uint32_t kImpactMs = 220u;
  content::BlendCanvas *canvas = content::BlendCanvasOf(d);
  const pixoo::PrecipitationKinematics kinematics =
      pixoo::PrecipitationKinematicsFor(pixoo::PrecipitationKind::HAIL,
                                        intensity);
  const uint32_t fall_ms = kinematics.fall_period_ms - kImpactMs;
  const Color hail = Blend(Color(166, 196, 222), Color(232, 242, 246),
                           scene.dayness);
  for (uint8_t i = 0; i < kCount; i++) {
    const uint32_t seed = Hash(0x1b873593u + i * 0x85ebca6bu);
    const ParticleClock clock =
        ParticleClockAt(elapsed_ms, kinematics.fall_period_ms, seed);
    const int x = ParticleX(i, kCount, seed, clock.cycle, clock.phase,
                            kinematics.fall_period_ms, kinematics.drift_px);
    const int impact_y = GroundYAt(x);
    const float fade = ParticlePresence(seed, weight);
    if (fade <= 0.0f)
      continue;
    if (clock.phase < fall_ms) {
      const int y = ParticleY(clock.phase, fall_ms, -1, impact_y - 2);
      DrawParticleSprite(d, canvas, x, y, hail, fade, kHailSprite);
      continue;
    }

    // Hail rebounds as one intact pellet. A horizontal impact burst reads as a
    // liquid splash and belongs to rain, not ice.
    const uint32_t bounce = clock.phase - fall_ms;
    const int height = static_cast<int>(bounce < kImpactMs / 2u
                                            ? bounce * 4u / (kImpactMs / 2u)
                                            : (kImpactMs - bounce) * 4u /
                                                  (kImpactMs / 2u));
    DrawParticleSprite(d, canvas, x, impact_y - 2 - height, hail, fade,
                       kHailSprite);
  }
}

void DrawPrecipitation(display::Display &d, const Scene &scene,
                       const pixoo::PrecipitationLayer &layer,
                       uint64_t elapsed_ms) {
  using pixoo::PrecipitationIntensity;
  using pixoo::PrecipitationKind;
  if (layer.weight <= 0.0f)
    return;
  switch (layer.kind) {
    case PrecipitationKind::SNOW:
      DrawSnow(d, scene, elapsed_ms, layer.intensity, layer.weight);
      return;
    case PrecipitationKind::SNOW_GRAINS:
      DrawSnowGrains(d, scene, elapsed_ms, layer.intensity, layer.weight);
      return;
    case PrecipitationKind::HAIL:
      DrawHail(d, scene, elapsed_ms, layer.intensity, layer.weight);
      return;
    case PrecipitationKind::NONE:
      return;
    case PrecipitationKind::DRIZZLE:
    case PrecipitationKind::RAIN:
      break;
  }

  const bool drizzle = layer.kind == PrecipitationKind::DRIZZLE;
  const bool heavy = layer.intensity == PrecipitationIntensity::HEAVY;
  content::BlendCanvas *canvas = content::BlendCanvasOf(d);
  const pixoo::PrecipitationKinematics kinematics =
      pixoo::PrecipitationKinematicsFor(layer.kind, layer.intensity);
  const uint32_t count = drizzle ? 16u : heavy ? 42u : 25u;
  const int length = drizzle ? 2 : heavy ? 6 : 4;
  const uint8_t head_opacity = drizzle ? 150u : heavy ? 225u : 205u;
  const uint8_t tail_opacity = drizzle ? 65u : 75u;
  const uint32_t period_ms = kinematics.fall_period_ms;
  const Color drop = PrecipitationColor(scene, layer.freezing, heavy);
  for (uint32_t i = 0; i < count; i++) {
    const uint32_t seed = Hash(0x6d2b79f5u + i);
    const ParticleClock clock = ParticleClockAt(elapsed_ms, period_ms, seed);
    const int x = ParticleX(i, count, seed, clock.cycle, clock.phase,
                            period_ms, kinematics.drift_px);
    const int y = ParticleY(clock.phase, period_ms, -1, GroundYAt(x) - 1);
    const float fade = ParticlePresence(seed, layer.weight);
    if (fade <= 0.0f)
      continue;
    for (int p = 0; p < length; p++) {
      const int px = x - p / 2;
      const int py = y - p;
      const float opacity =
          (head_opacity - p * (head_opacity - tail_opacity) /
                              std::max(1, length - 1)) /
          255.0f * fade;
      BlendAt(d, canvas, px, py, drop, opacity);
      if (!drizzle)
        BlendAt(d, canvas, px + 1, py, drop, opacity * 0.25f);
    }
  }
}

// Settled snow follows how much snow is currently falling, so a cover builds
// up and melts away with the field that produces it.
void DrawSnowCover(display::Display &d, const Scene &scene,
                   pixoo::PrecipitationKind kind, float amount) {
  if (amount <= 0.0f)
    return;
  const bool flakes = kind == pixoo::PrecipitationKind::SNOW;
  const Color day = flakes ? Color(220, 233, 238) : Color(192, 214, 226);
  const Color night = flakes ? Color(132, 166, 195) : Color(112, 145, 177);
  const Color snow = Blend(night, day, scene.dayness);
  content::BlendCanvas *canvas = content::BlendCanvasOf(d);

  if (flakes) {
    // Snow blankets the whole pitched roof. The roof's own width function and
    // fractional edge coverage keep the mantle aligned with the tiles.
    for (int y = HouseRoofPeakY(); y <= HouseEaveY(); y++) {
      const float half = RoofHalfWidthAt(y);
      const int x0 = static_cast<int>(std::floor(kHouseMidC - half - 1.0f));
      const int x1 = static_cast<int>(std::ceil(kHouseMidC + half + 1.0f));
      for (int x = x0; x <= x1; x++) {
        const float distance = std::fabs(x + 0.5f - kHouseMidC);
        BlendAt(d, canvas, x, y, snow,
                            RoofCoverage(distance, half) * 0.92f * amount);
      }
    }

    // A shallow cap follows the upper contour of the existing round canopy.
    // The search is bounded to the canopy's own columns and shares the tree
    // renderer's coverage function, so the cap has the same soft rim as the
    // foliage under it without a second geometry table.
    const float ccx = TreeCanopyCenterX();
    const float ccy = TreeCanopyCenterY();
    const int canopy_x0 = static_cast<int>(std::floor(ccx - kCanopyReach));
    const int canopy_x1 = static_cast<int>(std::ceil(ccx + kCanopyReach));
    const int canopy_y0 = static_cast<int>(std::floor(ccy - kCanopyReach));
    const int canopy_y1 = static_cast<int>(std::floor(ccy));
    for (int x = canopy_x0; x <= canopy_x1; x++) {
      const float rx = x + 0.5f - ccx;
      for (int y = canopy_y0; y <= canopy_y1; y++) {
        const float ry = y + 0.5f - ccy;
        const float coverage = CanopyCoverage(std::sqrt(rx * rx + ry * ry));
        if (coverage <= 0.0f)
          continue;
        BlendAt(d, canvas, x, y, snow, coverage * 0.92f * amount);
        const float below_y = y + 1.5f - ccy;
        BlendAt(d, canvas, x, y + 1, snow,
                CanopyCoverage(std::sqrt(rx * rx + below_y * below_y)) *
                    0.47f * amount);
        break;
      }
    }

    // Snow covers the complete visible terrain. A bright, slightly uneven cap
    // traces the hill while two cooler body shades preserve its depth.
    const Color snow_mid = Blend(snow, SnowColor(scene, 0u), 0.18f);
    const Color snow_deep = Blend(snow, SnowColor(scene, 0u), 0.34f);
    for (int x = 0; x < pixoo::kWidth; x++) {
      const uint32_t h = Hash(0x4cf5ad43u + static_cast<uint32_t>(x));
      const int ground_y = GroundYAt(x);
      BlendAt(d, canvas, x, ground_y - 1, snow,
                          (205u + (h & 31u)) / 255.0f * amount);
      if ((h & 7u) == 0u)
        BlendAt(d, canvas, x, ground_y - 2, snow, 0.37f * amount);
      for (int y = ground_y; y < pixoo::kHeight; y++) {
        const Color body = y - ground_y < 2 ? snow_mid : snow_deep;
        BlendAt(d, canvas, x, y, body, amount);
      }
    }
    return;
  }

  // Snow grains leave only a light, soft dusting on the open ground.
  for (int x = 0; x < pixoo::kWidth; x++) {
    const uint32_t h = Hash(0x4cf5ad43u + static_cast<uint32_t>(x));
    if ((h & 7u) != 0u)
      continue;
    BlendAt(d, canvas, x, GroundYAt(x) - 1, snow, 0.59f * amount);
  }
}

void DrawWetGround(display::Display &d, const Scene &scene, uint64_t elapsed_ms,
                   bool heavy, float amount) {
  if (amount <= 0.0f)
    return;
  content::BlendCanvas *canvas = content::BlendCanvasOf(d);
  const Color day = heavy ? Color(108, 142, 155) : Color(122, 156, 164);
  const Color night = heavy ? Color(56, 82, 94) : Color(72, 100, 110);
  const Color glint = Blend(night, day, scene.dayness);
  const int count = heavy ? 6 : 4;
  for (int i = 0; i < count; i++) {
    const uint32_t seed = 0x8f31u + static_cast<uint32_t>(i) * 0x41u;
    const uint32_t phase = pixoo::AnimationPhase(elapsed_ms, 4096u, seed);
    if (phase > 900u)
      continue;
    const int x = static_cast<int>(Hash(seed) % 60u) + 2;
    // Puddles gather on the earth below the surface of that column.
    const int y = GroundYAt(x) + 1 +
                  static_cast<int>((Hash(seed ^ 0xa5u) >> 8) % 3u);
    if (y >= pixoo::kHeight)
      continue;
    BlendAt(d, canvas, x, y, glint, 0.69f * amount);
    BlendAt(d, canvas, x + 1, y, glint, 0.43f * amount);
    if (phase > 450u)
      BlendAt(d, canvas, x - 1, y, glint, 0.29f * amount);
  }
}

void DrawIceHighlights(display::Display &d, const Scene &scene,
                       uint64_t elapsed_ms, float amount) {
  if (amount <= 0.0f)
    return;
  content::BlendCanvas *canvas = content::BlendCanvasOf(d);
  // Glazed roof tiles, as offsets down the roof from its peak, plus glints on
  // the open ground, each resting on its own column's surface.
  constexpr int kRoofDx[] = {-1, 1, -2, 2};
  constexpr int kRoofDy[] = {4, 2, 6, 6};
  constexpr int kGroundX[] = {18, 29, 45, 58};
  constexpr int kGroundDy[] = {2, 4, 1, 3};
  const Color day(202, 231, 241);
  const Color night(128, 172, 207);
  const Color ice = Blend(night, day, scene.dayness);
  const int peak_y = HouseRoofPeakY();
  const int mid_x = (kHouseX0 + kHouseX1) / 2;
  for (int i = 0; i < 8; i++) {
    const uint32_t phase = pixoo::AnimationPhase(
        elapsed_ms, 3072u, 0xc93u + static_cast<uint32_t>(i) * 0x23u);
    if (phase > 700u)
      continue;
    const int x = i < 4 ? mid_x + kRoofDx[i] : kGroundX[i - 4];
    const int y = i < 4 ? peak_y + kRoofDy[i]
                        : GroundYAt(x) + kGroundDy[i - 4];
    if (y >= pixoo::kHeight)
      continue;
    BlendAt(d, canvas, x, y, ice, 0.80f * amount);
    if (phase < 280u)
      BlendAt(d, canvas, x + 1, y, ice, 0.37f * amount);
  }
}

// Ground cover settles under whichever field is currently producing it, so it
// is drawn once from the summed shares before any particle falls over it.
void DrawWorldEffects(display::Display &d, const Scene &scene,
                      const pixoo::WeatherEffectMix &mix,
                      uint64_t elapsed_ms) {
  DrawFog(d, scene, elapsed_ms, mix.fog);

  float snow_cover = 0.0f;
  float grain_cover = 0.0f;
  float wet = 0.0f;
  bool wet_heavy = false;
  for (uint8_t i = 0; i < mix.precipitation_count; i++) {
    const pixoo::PrecipitationLayer &layer = mix.precipitation[i];
    if (layer.kind == pixoo::PrecipitationKind::SNOW)
      snow_cover += layer.weight;
    else if (layer.kind == pixoo::PrecipitationKind::SNOW_GRAINS)
      grain_cover += layer.weight;
    else if (layer.kind == pixoo::PrecipitationKind::RAIN && !layer.freezing) {
      wet += layer.weight;
      wet_heavy = wet_heavy ||
                  layer.intensity == pixoo::PrecipitationIntensity::HEAVY;
    }
  }
  DrawSnowCover(d, scene, pixoo::PrecipitationKind::SNOW_GRAINS,
                Clamp01(grain_cover));
  DrawSnowCover(d, scene, pixoo::PrecipitationKind::SNOW, Clamp01(snow_cover));

  for (uint8_t i = 0; i < mix.precipitation_count; i++)
    DrawPrecipitation(d, scene, mix.precipitation[i], elapsed_ms);

  DrawWetGround(d, scene, elapsed_ms, wet_heavy, Clamp01(wet));
  DrawIceHighlights(d, scene, elapsed_ms, mix.freezing);
}

// Temperature drawn directly over the scene with a 1px black outline, so it
// stays legible on any sky without a box behind it.
void DrawTemperature(display::Display &d, const WeatherViewModel &v,
                     const WeatherFonts &fonts) {
  if (!v.has_temperature || fonts.big == nullptr)
    return;
  char buf[8];
  std::snprintf(buf, sizeof(buf), "%d\xC2\xB0",
                pixoo::DisplayDegrees(v.temperature));
  const int x = 2;
  const int y = pixoo::kHeight - 15;
  const Color outline(0, 0, 0);
  for (int oy = -1; oy <= 1; oy++)
    for (int ox = -1; ox <= 1; ox++)
      if (ox != 0 || oy != 0)
        d.print(x + ox, y + oy, fonts.big, outline,
                display::TextAlign::TOP_LEFT, buf);
  d.print(x, y, fonts.big, kText, display::TextAlign::TOP_LEFT, buf);
}

}  // namespace

void LandscapeFace::OnShow(uint32_t now_ms) {
  this->effect_clock_.Resume(now_ms);
}

void LandscapeFace::Tick(const WeatherViewModel &view, uint32_t now_ms) {
  this->effect_clock_.Tick(now_ms);
  this->effects_.Update(view.condition, this->effect_clock_.elapsed_ms());
}

void LandscapeFace::Render(display::Display &d, const WeatherViewModel &v,
                                const WeatherFonts &fonts) const {
  const Scene scene = MakeScene(v.sky);
  const float dn = scene.dayness;
  const uint64_t effects_ms = this->effect_clock_.elapsed_ms();
  // Every condition trait resolves to a scene quantity, so one scene state is
  // drawn per frame and a handoff moves those quantities rather than
  // compositing two complete conditions.
  const pixoo::WeatherEffectMix mix =
      this->effects_.initialized()
          ? this->effects_.Mix()
          : pixoo::MixWeatherEffects(pixoo::WeatherEffectFor(v.condition),
                                     pixoo::WeatherEffectFor(v.condition),
                                     1.0f);

  // One storm timeline serves the whole scene, so a handoff into or out of a
  // storm changes how present the flash is, never how many bolts strike.
  const pixoo::LightningState lightning =
      pixoo::LightningAt(mix.storm > 0.0f, effects_ms);
  const float lightning_intensity =
      static_cast<float>(lightning.intensity) * mix.storm;

  FillSky(d, scene);

  // Sun position: glides left to right by day_fraction on a shallow bow, and
  // sinks toward the horizon as it fades through twilight.
  const float sun_x = Lerp(kSunEdgeL, kSunEdgeR, Clamp01(v.sky.day_fraction));
  const float sun_y = kSunBaseY - kSunRise * std::sin(v.sky.day_fraction * kPi) +
                      (1.0f - dn) * kSunSink;
  // Moon position: local rise is left, transit near centre, and set right.
  // Actual altitude drives a shallow bounded display arc; the disc fades in
  // across the first few degrees above the mathematical horizon.
  const float moon_x =
      Lerp(kSunEdgeL, kSunEdgeR, Clamp01(v.sky.moon_arc_fraction));
  const float moon_altitude =
      std::max(0.0f, std::min(90.0f, v.sky.moon_altitude_degrees));
  const float moon_y =
      kMoonBaseY - kMoonRise * std::sin(moon_altitude * kPi / 180.0f);
  const float moon_horizon_alpha =
      v.sky.valid && v.sky.moon_above_horizon
          ? Clamp01(moon_altitude / kMoonHorizonFadeDegrees)
          : 0.0f;
  // Daylight subdues an above-horizon Moon; DrawMoon separately removes its
  // unlit portion from the bright sky.
  const float moon_alpha =
      moon_horizon_alpha * Lerp(1.0f, kMoonDayAlpha, dn);

  DrawStars(d, scene, 1.0f - dn, effects_ms, sun_x, sun_y, dn);
  DrawMoon(d, scene, moon_x, moon_y, v.sky.moon_illumination,
           v.sky.moon_waxing, moon_alpha, dn);
  DrawSun(d, scene, sun_x, sun_y, dn, SunGlowPulse(effects_ms));

  // Apply the flash after the celestial bodies so it reaches them, but before
  // clouds and the bolt so those remain foreground elements.
  DrawLightningSkyGlow(d, scene, lightning, mix.storm);
  DrawClouds(d, scene, mix.cloud_cover, effects_ms, lightning_intensity);
  DrawLightningBolt(d, scene, lightning, mix.storm);

  // Landscape light combines only active celestial sources. Moonlight is zero
  // below the horizon and phase-dependent above it; ambient night lighting is
  // independent, so a moonless night still has readable depth. The high lunar
  // source point keeps moonlight overhead instead of strongly side-lighting the
  // scene from the large display disc.
  const float phase_moon_strength =
      kMoonStrengthPeak * Clamp01(v.sky.moon_illumination);
  const float moon_light_strength =
      (1.0f - dn) * moon_horizon_alpha * phase_moon_strength;
  const float sun_light_strength = dn;
  const float celestial_strength = moon_light_strength + sun_light_strength;
  float sun_mix = 0.0f;
  float lx = 32.0f;
  float ly = kMoonLightY;
  if (celestial_strength > 0.0f) {
    sun_mix = sun_light_strength / celestial_strength;
    lx = Lerp(moon_x, sun_x, sun_mix);
    ly = Lerp(kMoonLightY, sun_y, sun_mix);
  }
  const float lightning_level = lightning_intensity /
                                pixoo::kLightningMaxIntensity;
  const Color light = Blend(Blend(kMoonlight, kSunLight, sun_mix),
                            Color(186, 205, 244), lightning_level * 0.42f);
  const float strength = celestial_strength + lightning_level * 0.22f;
  const float ambient = Lerp(0.30f, 0.5f, dn) + lightning_level * 0.16f;
  DrawLandscape(d, lx, ly, light, strength, ambient);

  // World effects overlay the horizon and landscape; temperature is current
  // data and therefore draws above them.
  DrawWorldEffects(d, scene, mix, effects_ms);
  DrawTemperature(d, v, fonts);
}

}  // namespace esphome::pixoo64::dashboard::weather
