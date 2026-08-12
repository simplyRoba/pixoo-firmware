#include "seven_segment.h"

#include "esphome/components/pixoo64_content/aa_draw.h"
#include "esphome/components/pixoo64_content/blend_canvas.h"

namespace esphome::pixoo64::seven_segment {
namespace {

Color PopColor(Color color, float excess, float gain) {
  const float amount = excess * gain;
  return amount > 0.0f
             ? content::Blend(color, Color(255, 255, 255), amount)
             : color;
}

}  // namespace

void DrawLedStroke(display::Display &display, float x0, float y0, float x1,
                   float y1, const LedStyle &style, float level) {
  const float settled = content::Clamp01(level);
  if (settled <= 0.0f)
    return;
  const float excess = level > 1.0f ? level - 1.0f : 0.0f;
  const float growth = excess * style.pop_radius_gain;
  content::BlendCanvas *canvas = content::BlendCanvasOf(display);
  content::StrokeCapsule(
      display, canvas, x0, y0, x1, y1, style.glow_radius + growth,
      style.glow_radius + growth,
      PopColor(style.glow, excess, style.pop_whiten_gain), 0.20f * settled);
  content::StrokeCapsule(
      display, canvas, x0, y0, x1, y1, style.radius + growth,
      style.radius + growth,
      PopColor(style.body, excess, style.pop_whiten_gain), settled);
  content::StrokeCapsule(
      display, canvas, x0, y0, x1, y1, style.core_radius + growth,
      style.core_radius + growth,
      PopColor(style.core, excess, style.pop_whiten_gain), 0.56f * settled);
}

void DrawLedDot(display::Display &display, float x, float y,
                const LedStyle &style, float level) {
  const float settled = content::Clamp01(level);
  if (settled <= 0.0f)
    return;
  const float excess = level > 1.0f ? level - 1.0f : 0.0f;
  const float growth = excess * style.pop_radius_gain;
  content::BlendCanvas *canvas = content::BlendCanvasOf(display);
  content::FillDisc(
      display, canvas, content::Disc{x, y, style.glow_radius + growth},
      PopColor(style.glow, excess, style.pop_whiten_gain), 0.20f * settled);
  content::FillDisc(display, canvas,
                    content::Disc{x, y, style.radius + growth},
                    PopColor(style.body, excess, style.pop_whiten_gain),
                    settled);
  content::FillDisc(
      display, canvas, content::Disc{x, y, style.core_radius + growth},
      PopColor(style.core, excess, style.pop_whiten_gain), 0.56f * settled);
}

void DrawSegment(display::Display &display, float x, float y, float width,
                 float height, uint8_t segment, const LedStyle &style,
                 float level) {
  const float middle = y + height * 0.5f;
  const float bottom = y + height;
  switch (segment) {
    case kSegmentA:
      DrawLedStroke(display, x + 2.2f, y + 1.0f, x + width - 0.7f,
                    y + 1.0f, style, level);
      break;
    case kSegmentB:
      DrawLedStroke(display, x + width, y + 3.1f, x + width - 0.7f,
                    middle - 2.0f, style, level);
      break;
    case kSegmentC:
      DrawLedStroke(display, x + width - 0.9f, middle + 2.0f,
                    x + width - 1.6f, bottom - 3.0f, style, level);
      break;
    case kSegmentD:
      DrawLedStroke(display, x + 0.7f, bottom - 1.0f, x + width - 2.2f,
                    bottom - 1.0f, style, level);
      break;
    case kSegmentE:
      DrawLedStroke(display, x + 0.3f, middle + 2.0f, x - 0.4f,
                    bottom - 3.0f, style, level);
      break;
    case kSegmentF:
      DrawLedStroke(display, x + 1.2f, y + 3.1f, x + 0.5f,
                    middle - 2.0f, style, level);
      break;
    case kSegmentG:
      DrawLedStroke(display, x + 1.5f, middle, x + width - 1.5f, middle,
                    style, level);
      break;
    case kDecimalPoint:
      DrawLedDot(display, x + width + 1.0f, bottom - 1.0f, style, level);
      break;
    default:
      break;
  }
}

void DrawDigit(display::Display &display, float x, float y, float width,
               float height, uint8_t segments, const LedStyle &style,
               float level) {
  constexpr uint8_t kOrderedSegments[] = {
      kSegmentA, kSegmentB, kSegmentC, kSegmentD,
      kSegmentE, kSegmentF, kSegmentG, kDecimalPoint,
  };
  for (uint8_t segment : kOrderedSegments) {
    if ((segments & segment) != 0)
      DrawSegment(display, x, y, width, height, segment, style, level);
  }
}

}  // namespace esphome::pixoo64::seven_segment
