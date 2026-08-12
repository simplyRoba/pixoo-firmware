#pragma once

#include <cstdint>

#include "digital_clock.h"
#include "esphome/components/display/display.h"
#include "esphome/core/color.h"

namespace esphome::pixoo64::seven_segment {

constexpr uint8_t kSegmentA = pixoo::clock::kDigitalSegmentA;
constexpr uint8_t kSegmentB = pixoo::clock::kDigitalSegmentB;
constexpr uint8_t kSegmentC = pixoo::clock::kDigitalSegmentC;
constexpr uint8_t kSegmentD = pixoo::clock::kDigitalSegmentD;
constexpr uint8_t kSegmentE = pixoo::clock::kDigitalSegmentE;
constexpr uint8_t kSegmentF = pixoo::clock::kDigitalSegmentF;
constexpr uint8_t kSegmentG = pixoo::clock::kDigitalSegmentG;
constexpr uint8_t kDecimalPoint = 1u << 7;

struct LedStyle {
  float radius;
  float glow_radius;
  float core_radius;
  Color glow;
  Color body;
  Color core;
  // Radius and whitening gains per unit of animated level above 1.
  float pop_radius_gain{0.0f};
  float pop_whiten_gain{0.0f};
};

void DrawLedStroke(display::Display &display, float x0, float y0, float x1,
                   float y1, const LedStyle &style, float level = 1.0f);
void DrawLedDot(display::Display &display, float x, float y,
                const LedStyle &style, float level = 1.0f);

// Draws one segment bit at the same geometry used by DrawDigit().
void DrawSegment(display::Display &display, float x, float y, float width,
                 float height, uint8_t segment, const LedStyle &style,
                 float level = 1.0f);
void DrawDigit(display::Display &display, float x, float y, float width,
               float height, uint8_t segments, const LedStyle &style,
               float level = 1.0f);

}  // namespace esphome::pixoo64::seven_segment
