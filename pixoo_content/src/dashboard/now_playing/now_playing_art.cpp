#include "now_playing_art.h"

#include <algorithm>
#include <cmath>

namespace pixoo::now_playing {
namespace {

uint64_t SplitMix64(uint64_t value) {
  value += 0x9e3779b97f4a7c15ull;
  value = (value ^ (value >> 30)) * 0xbf58476d1ce4e5b9ull;
  value = (value ^ (value >> 27)) * 0x94d049bb133111ebull;
  return value ^ (value >> 31);
}

struct Rgb {
  float red;
  float green;
  float blue;
};

struct LabelPalette {
  Rgb light;
  Rgb dark;
};

constexpr LabelPalette kLabelPalettes[] = {
    {{255, 95, 146}, {126, 36, 126}},
    {{255, 173, 66}, {207, 55, 72}},
    {{72, 224, 211}, {32, 102, 190}},
    {{184, 128, 255}, {72, 73, 200}},
    {{165, 230, 85}, {24, 143, 116}},
    {{255, 112, 72}, {184, 48, 150}},
    {{87, 192, 255}, {40, 73, 193}},
    {{255, 215, 82}, {204, 92, 45}},
};

float Clamp01(float value) {
  return std::max(0.0f, std::min(1.0f, value));
}

Rgb Mix(Rgb from, Rgb to, float amount) {
  const float a = Clamp01(amount);
  return {from.red + (to.red - from.red) * a,
          from.green + (to.green - from.green) * a,
          from.blue + (to.blue - from.blue) * a};
}

float DistanceSquaredToSegment(float px, float py, float ax, float ay,
                               float bx, float by) {
  const float vx = bx - ax;
  const float vy = by - ay;
  const float length_squared = vx * vx + vy * vy;
  const float projection =
      length_squared == 0.0f
          ? 0.0f
          : Clamp01(((px - ax) * vx + (py - ay) * vy) / length_squared);
  const float dx = px - (ax + projection * vx);
  const float dy = py - (ay + projection * vy);
  return dx * dx + dy * dy;
}

Rgb SceneSample(float x, float y, const LabelPalette &palette) {
  const float backdrop_gradient = y / 63.0f;
  Rgb color = {9.0f + 5.0f * backdrop_gradient,
               16.0f + 8.0f * backdrop_gradient,
               28.0f + 10.0f * backdrop_gradient};

  constexpr float record_x = 24.0f;
  constexpr float record_y = 24.0f;
  const float record_dx = x - record_x;
  const float record_dy = y - record_y;
  const float record_radius =
      std::sqrt(record_dx * record_dx + record_dy * record_dy);

  // A cool halo and a separate drop shadow keep the dark vinyl readable on a
  // dark display without turning it grey.
  const float halo = Clamp01((32.0f - record_radius) / 12.0f);
  color.red += 3.0f * halo;
  color.green += 7.0f * halo;
  color.blue += 12.0f * halo;
  const float shadow_dx = x - 26.0f;
  const float shadow_dy = y - 27.0f;
  if (shadow_dx * shadow_dx + shadow_dy * shadow_dy <= 24.5f * 24.5f)
    color = Mix(color, {1, 3, 7}, 0.48f);

  if (record_radius <= 23.5f) {
    if (record_radius >= 22.35f) {
      color = {105, 139, 158};
    } else {
      const float diagonal_gloss =
          Clamp01(1.0f - std::fabs(record_dx - record_dy + 4.0f) / 8.0f);
      const float groove_position = std::fmod(record_radius + 0.2f, 3.15f);
      const float groove =
          Clamp01(1.0f - std::min(groove_position, 3.15f - groove_position) /
                             0.48f);
      const float edge_light = Clamp01((record_radius - 18.0f) / 4.35f);
      color = {25.0f + 8.0f * diagonal_gloss + 13.0f * groove + 8.0f * edge_light,
               32.0f + 9.0f * diagonal_gloss + 16.0f * groove + 11.0f * edge_light,
               42.0f + 11.0f * diagonal_gloss + 19.0f * groove + 14.0f * edge_light};
    }
  }

  if (record_radius <= 7.8f) {
    const float label_gradient = Clamp01((record_dy + 7.8f) / 15.6f);
    color = Mix(palette.light, palette.dark, label_gradient);
    if (record_radius >= 6.7f)
      color = Mix(color, {242, 230, 222}, 0.32f);
  }
  if (record_radius <= 1.35f)
    color = {244, 248, 242};

  // The tonearm is built from a shaded base, two-part metal shaft, cartridge,
  // and stylus. Its dark offset is a shadow, not another structural segment.
  const float arm_shadow = std::min(
      DistanceSquaredToSegment(x, y, 51.0f, 14.5f, 45.0f, 21.5f),
      DistanceSquaredToSegment(x, y, 45.0f, 21.5f, 39.0f, 30.5f));
  if (arm_shadow <= 3.2f * 3.2f)
    color = Mix(color, {1, 3, 6}, 0.62f);

  const float arm = std::min(
      DistanceSquaredToSegment(x, y, 49.5f, 13.0f, 43.5f, 20.0f),
      DistanceSquaredToSegment(x, y, 43.5f, 20.0f, 37.5f, 29.0f));
  if (arm <= 2.05f * 2.05f)
    color = {79, 101, 113};
  if (arm <= 1.15f * 1.15f)
    color = {190, 211, 216};
  if (arm <= 0.42f * 0.42f)
    color = {244, 251, 248};

  const float cartridge =
      DistanceSquaredToSegment(x, y, 39.0f, 27.7f, 36.4f, 31.3f);
  if (cartridge <= 2.4f * 2.4f)
    color = {37, 44, 52};
  if (cartridge <= 1.35f * 1.35f)
    color = Mix(palette.dark, palette.light, 0.55f);
  const float stylus =
      DistanceSquaredToSegment(x, y, 36.5f, 31.0f, 35.3f, 33.8f);
  if (stylus <= 0.65f * 0.65f)
    color = {226, 235, 232};

  const float pivot_dx = x - 53.0f;
  const float pivot_dy = y - 9.0f;
  const float pivot_radius =
      std::sqrt(pivot_dx * pivot_dx + pivot_dy * pivot_dy);
  if (pivot_radius <= 7.2f)
    color = {31, 43, 53};
  if (pivot_radius <= 5.7f)
    color = {111, 139, 149};
  if (pivot_radius <= 4.3f)
    color = {40, 55, 65};
  if (pivot_radius <= 2.5f)
    color = {181, 201, 203};
  if ((x - 52.0f) * (x - 52.0f) + (y - 8.0f) * (y - 8.0f) <= 1.2f)
    color = {246, 251, 245};

  return color;
}

}  // namespace

uint16_t Rgb565(uint8_t red, uint8_t green, uint8_t blue) {
  return static_cast<uint16_t>((static_cast<uint16_t>(red >> 3) << 11) |
                               (static_cast<uint16_t>(green >> 2) << 5) |
                               static_cast<uint16_t>(blue >> 3));
}

bool GenerateArtworkPlaceholder(uint64_t identity, uint16_t *destination,
                                size_t destination_count) {
  if (destination == nullptr || destination_count < kArtworkPixelCount)
    return false;
  const uint64_t seed = SplitMix64(identity ^ 0x415254504c414345ull);
  LabelPalette palette =
      kLabelPalettes[seed % (sizeof(kLabelPalettes) / sizeof(kLabelPalettes[0]))];
  palette.light.red += static_cast<int>((seed >> 8) & 0x1f) - 15;
  palette.light.green += static_cast<int>((seed >> 16) & 0x1f) - 15;
  palette.dark.blue += static_cast<int>((seed >> 24) & 0x1f) - 15;

  // Four subpixel samples preserve the round record, diagonal shaft, and small
  // stylus on the coarse panel grid without storing a second working image.
  constexpr float sample_offsets[] = {0.25f, 0.75f};
  for (int y = 0; y < static_cast<int>(kArtworkHeight); ++y) {
    for (int x = 0; x < static_cast<int>(kArtworkWidth); ++x) {
      Rgb sum{0, 0, 0};
      for (float offset_y : sample_offsets) {
        for (float offset_x : sample_offsets) {
          const Rgb sample = SceneSample(x + offset_x, y + offset_y, palette);
          sum.red += sample.red;
          sum.green += sample.green;
          sum.blue += sample.blue;
        }
      }
      const auto channel = [](float value) {
        return static_cast<uint8_t>(
            std::max(0.0f, std::min(255.0f, value * 0.25f)) + 0.5f);
      };
      destination[y * kArtworkWidth + x] =
          Rgb565(channel(sum.red), channel(sum.green), channel(sum.blue));
    }
  }
  return true;
}

}  // namespace pixoo::now_playing
