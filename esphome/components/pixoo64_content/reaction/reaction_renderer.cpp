#include "reaction_renderer.h"

#include <algorithm>
#include <cmath>

#include "esphome/components/pixoo64_content/blend_canvas.h"
#include "reaction_art.h"

namespace esphome::pixoo64::content {
namespace {

constexpr float kPi = 3.14159265358979323846f;

struct Motion {
  float center_x{31.5f};
  float center_y{31.5f};
  float scale{0.92f};
  float angle_radians{0.0f};
  float opacity{1.0f};
};

struct Sample {
  float red{0.0f};
  float green{0.0f};
  float blue{0.0f};
  float alpha{0.0f};
};

float Clamp01(float value) {
  return std::max(0.0f, std::min(1.0f, value));
}

float SmoothStep(float value) {
  value = Clamp01(value);
  return value * value * (3.0f - 2.0f * value);
}

float Wave(float cycles, float progress, float phase = 0.0f) {
  return std::sin((cycles * progress + phase) * 2.0f * kPi);
}

Motion MotionFor(pixoo::Reaction reaction, float progress) {
  Motion motion;
  const float enter = SmoothStep(progress / 0.16f);
  const float leave = SmoothStep((1.0f - progress) / 0.14f);
  motion.opacity = std::min(enter, leave);
  motion.scale *= 0.58f + 0.42f * enter;

  switch (reaction) {
    case pixoo::Reaction::kLaughing:
      motion.center_y -= 2.7f * std::fabs(Wave(2.0f, progress));
      motion.scale *= 1.0f + 0.045f * Wave(3.0f, progress);
      motion.angle_radians = Wave(2.0f, progress) * 5.0f * kPi / 180.0f;
      break;
    case pixoo::Reaction::kLove: {
      const float beat = std::pow(std::max(0.0f, Wave(3.0f, progress)), 6.0f);
      motion.scale *= 0.94f + 0.10f * beat;
      motion.center_y += 0.8f * Wave(1.0f, progress);
      break;
    }
    case pixoo::Reaction::kCrying:
      motion.center_y = 27.5f + 6.5f * progress;
      motion.center_x += 1.3f * Wave(1.5f, progress);
      motion.angle_radians = Wave(1.0f, progress) * 2.5f * kPi / 180.0f;
      break;
    case pixoo::Reaction::kAngry:
      motion.center_x += (1.0f - 0.55f * progress) * 2.4f * Wave(8.0f, progress);
      motion.scale *= 0.96f + 0.05f * progress;
      motion.angle_radians = Wave(8.0f, progress) * 2.0f * kPi / 180.0f;
      break;
    case pixoo::Reaction::kPoop:
      motion.center_y += 3.2f - 7.0f * std::fabs(Wave(1.0f, progress));
      motion.scale *= 1.0f + 0.05f * Wave(2.0f, progress, 0.25f);
      motion.angle_radians = Wave(1.0f, progress) * 4.0f * kPi / 180.0f;
      break;
    case pixoo::Reaction::kApprove:
      motion.center_y += 5.0f * (1.0f - enter) - 1.5f * Wave(1.0f, progress);
      motion.angle_radians = (-7.0f + 4.0f * enter) * kPi / 180.0f;
      break;
    case pixoo::Reaction::kDisapprove:
      motion.center_y -= 4.0f * (1.0f - enter);
      motion.center_y += 3.5f * progress;
      motion.angle_radians = (7.0f - 4.0f * enter) * kPi / 180.0f;
      break;
    case pixoo::Reaction::kCelebrate:
      motion.scale *= 0.94f + 0.06f * Wave(2.0f, progress, 0.25f);
      motion.angle_radians = (-9.0f + 18.0f * progress) * kPi / 180.0f;
      motion.center_y -= 1.5f * std::fabs(Wave(2.0f, progress));
      break;
    case pixoo::Reaction::kThinking:
      motion.center_y += 1.8f * Wave(0.75f, progress);
      motion.angle_radians = Wave(0.75f, progress, 0.25f) * 4.0f * kPi / 180.0f;
      motion.scale *= 0.97f;
      break;
    case pixoo::Reaction::kSurprised:
      motion.scale *= 0.94f + 0.075f * Wave(2.5f, progress, 0.25f);
      motion.center_y += 1.0f * Wave(2.5f, progress);
      break;
    case pixoo::Reaction::kFire:
      motion.center_y += 2.0f * progress - 2.0f * Wave(1.5f, progress);
      motion.center_x += 1.0f * Wave(4.0f, progress);
      motion.scale *= 0.96f + 0.045f * Wave(5.0f, progress, 0.25f);
      motion.angle_radians = Wave(3.0f, progress) * 3.0f * kPi / 180.0f;
      break;
    case pixoo::Reaction::kEyes:
      motion.center_x += 5.0f * Wave(0.75f, progress);
      motion.center_y += 1.0f * Wave(1.5f, progress);
      motion.scale *= 0.96f + 0.035f * Wave(1.5f, progress, 0.25f);
      break;
  }
  return motion;
}

Sample ReadSample(int art_index, int x, int y) {
  if (x < 0 || x >= reaction_art::kArtSize || y < 0 ||
      y >= reaction_art::kArtSize)
    return {};
  const int offset = (y * reaction_art::kArtSize + x) * 2;
  const uint8_t palette_index = reaction_art::kPixels[art_index][offset];
  const uint8_t alpha = reaction_art::kPixels[art_index][offset + 1];
  const uint16_t packed = reaction_art::kPalette[art_index][palette_index];
  return Sample{
      static_cast<float>(((packed >> 11) & 0x1F) * 255 / 31),
      static_cast<float>(((packed >> 5) & 0x3F) * 255 / 63),
      static_cast<float>((packed & 0x1F) * 255 / 31),
      static_cast<float>(alpha),
  };
}

Sample BilinearSample(int art_index, float x, float y) {
  const int x0 = static_cast<int>(std::floor(x));
  const int y0 = static_cast<int>(std::floor(y));
  const float fx = x - x0;
  const float fy = y - y0;
  const float weights[4] = {
      (1.0f - fx) * (1.0f - fy), fx * (1.0f - fy),
      (1.0f - fx) * fy, fx * fy,
  };
  const Sample samples[4] = {
      ReadSample(art_index, x0, y0), ReadSample(art_index, x0 + 1, y0),
      ReadSample(art_index, x0, y0 + 1),
      ReadSample(art_index, x0 + 1, y0 + 1),
  };

  Sample result;
  for (int i = 0; i < 4; ++i) {
    const float weighted_alpha = samples[i].alpha * weights[i];
    result.alpha += weighted_alpha;
    result.red += samples[i].red * weighted_alpha;
    result.green += samples[i].green * weighted_alpha;
    result.blue += samples[i].blue * weighted_alpha;
  }
  if (result.alpha > 0.0f) {
    result.red /= result.alpha;
    result.green /= result.alpha;
    result.blue /= result.alpha;
  }
  return result;
}

}  // namespace

void ReactionRenderer::Render(display::Display &display,
                              pixoo::Reaction reaction,
                              uint32_t visible_elapsed_ms) const {
  BlendCanvas *canvas = BlendCanvasOf(display);
  if (canvas == nullptr)
    return;

  const float duration_ms =
      static_cast<float>(pixoo::ReactionVisibleDurationMs(reaction));
  const float progress = Clamp01(visible_elapsed_ms / duration_ms);
  const Motion motion = MotionFor(reaction, progress);
  if (motion.opacity <= 0.0f || motion.scale <= 0.0f)
    return;

  const int art_index = static_cast<int>(reaction);
  if (art_index < 0 || art_index >= reaction_art::kReactionCount)
    return;

  const float cosine = std::cos(motion.angle_radians);
  const float sine = std::sin(motion.angle_radians);
  const float half_extent = 0.5f * reaction_art::kArtSize * motion.scale;
  const float radius = half_extent * (std::fabs(cosine) + std::fabs(sine)) + 1.0f;
  const int left = std::max(0, static_cast<int>(std::floor(motion.center_x - radius)));
  const int right = std::min(63, static_cast<int>(std::ceil(motion.center_x + radius)));
  const int top = std::max(0, static_cast<int>(std::floor(motion.center_y - radius)));
  const int bottom = std::min(63, static_cast<int>(std::ceil(motion.center_y + radius)));

  constexpr float kArtCenter = (reaction_art::kArtSize - 1) * 0.5f;
  for (int y = top; y <= bottom; ++y) {
    for (int x = left; x <= right; ++x) {
      const float dx = (x - motion.center_x) / motion.scale;
      const float dy = (y - motion.center_y) / motion.scale;
      const float source_x = cosine * dx + sine * dy + kArtCenter;
      const float source_y = -sine * dx + cosine * dy + kArtCenter;
      const Sample sample = BilinearSample(art_index, source_x, source_y);
      const float alpha = sample.alpha / 255.0f * motion.opacity;
      if (alpha <= 0.001f)
        continue;
      const uint16_t alpha_level = static_cast<uint16_t>(
          std::lround(std::clamp(alpha, 0.0f, 1.0f) * 65535.0f));
      const float stable_alpha = alpha_level / 65535.0f;
      canvas->BlendPixel(
          x, y,
          Color(static_cast<uint8_t>(sample.red + 0.5f),
                static_cast<uint8_t>(sample.green + 0.5f),
                static_cast<uint8_t>(sample.blue + 0.5f)),
          stable_alpha);
    }
  }
}

}  // namespace esphome::pixoo64::content
