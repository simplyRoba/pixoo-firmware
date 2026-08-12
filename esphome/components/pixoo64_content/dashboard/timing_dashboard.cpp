#include "timing_dashboard.h"

#include <array>
#include <cstdint>

#include "esphome/components/pixoo64_content/aa_draw.h"
#include "esphome/components/pixoo64_content/blend_canvas.h"

namespace esphome::pixoo64::dashboard {
namespace {
constexpr uint8_t kSegmentA = 1u << 0;
constexpr uint8_t kSegmentB = 1u << 1;
constexpr uint8_t kSegmentC = 1u << 2;
constexpr uint8_t kSegmentD = 1u << 3;
constexpr uint8_t kSegmentE = 1u << 4;
constexpr uint8_t kSegmentF = 1u << 5;
constexpr uint8_t kSegmentG = 1u << 6;
constexpr uint8_t kDecimalPoint = 1u << 7;
constexpr uint32_t kMaximumDisplayedMs =
    99u * 60u * 1000u + 59u * 1000u + 990u;
constexpr std::array<uint8_t, 10> kDigitSegments{
    kSegmentA | kSegmentB | kSegmentC | kSegmentD | kSegmentE | kSegmentF,
    kSegmentB | kSegmentC,
    kSegmentA | kSegmentB | kSegmentD | kSegmentE | kSegmentG,
    kSegmentA | kSegmentB | kSegmentC | kSegmentD | kSegmentG,
    kSegmentB | kSegmentC | kSegmentF | kSegmentG,
    kSegmentA | kSegmentC | kSegmentD | kSegmentF | kSegmentG,
    kSegmentA | kSegmentC | kSegmentD | kSegmentE | kSegmentF | kSegmentG,
    kSegmentA | kSegmentB | kSegmentC,
    kSegmentA | kSegmentB | kSegmentC | kSegmentD | kSegmentE | kSegmentF |
        kSegmentG,
    kSegmentA | kSegmentB | kSegmentC | kSegmentD | kSegmentF | kSegmentG,
};

struct LedStyle {
  float radius;
  float glow_radius;
  float core_radius;
  Color glow;
  Color body;
  Color core;
};

const Color kBackground(2, 0, 0);
const LedStyle kMainStyle{1.35f,
                          2.45f,
                          0.50f,
                          Color(154, 10, 3),
                          Color(238, 48, 25),
                          Color(255, 154, 94)};
const LedStyle kFractionStyle{1.00f,
                              1.80f,
                              0.38f,
                              Color(146, 48, 3),
                              Color(255, 146, 38),
                              Color(255, 224, 146)};
const Color kStoppedPunctuation(83, 19, 12);

void DrawLedStroke(display::Display &display, float x0, float y0, float x1,
                   float y1, const LedStyle &style) {
  content::BlendCanvas *canvas = content::BlendCanvasOf(display);
  content::StrokeCapsule(display, canvas, x0, y0, x1, y1,
                         style.glow_radius, style.glow_radius, style.glow,
                         0.20f);
  content::StrokeCapsule(display, canvas, x0, y0, x1, y1, style.radius,
                         style.radius, style.body);
  content::StrokeCapsule(display, canvas, x0, y0, x1, y1, style.core_radius,
                         style.core_radius, style.core, 0.56f);
}

void DrawLedDot(display::Display &display, float x, float y,
                const LedStyle &style) {
  content::BlendCanvas *canvas = content::BlendCanvasOf(display);
  content::FillDisc(display, canvas,
                    content::Disc{x, y, style.glow_radius}, style.glow, 0.20f);
  content::FillDisc(display, canvas, content::Disc{x, y, style.radius},
                    style.body);
  content::FillDisc(display, canvas,
                    content::Disc{x, y, style.core_radius}, style.core, 0.56f);
}

void DrawDigit(display::Display &display, float x, float y, float width,
               float height, uint8_t segments, const LedStyle &style) {
  const float middle = y + height * 0.5f;
  const float bottom = y + height;

  if ((segments & kSegmentA) != 0)
    DrawLedStroke(display, x + 2.2f, y + 1.0f, x + width - 0.7f, y + 1.0f,
                  style);
  if ((segments & kSegmentB) != 0)
    DrawLedStroke(display, x + width, y + 3.1f, x + width - 0.7f,
                  middle - 2.0f, style);
  if ((segments & kSegmentC) != 0)
    DrawLedStroke(display, x + width - 0.9f, middle + 2.0f,
                  x + width - 1.6f, bottom - 3.0f, style);
  if ((segments & kSegmentD) != 0)
    DrawLedStroke(display, x + 0.7f, bottom - 1.0f, x + width - 2.2f,
                  bottom - 1.0f, style);
  if ((segments & kSegmentE) != 0)
    DrawLedStroke(display, x + 0.3f, middle + 2.0f, x - 0.4f, bottom - 3.0f,
                  style);
  if ((segments & kSegmentF) != 0)
    DrawLedStroke(display, x + 1.2f, y + 3.1f, x + 0.5f, middle - 2.0f,
                  style);
  if ((segments & kSegmentG) != 0)
    DrawLedStroke(display, x + 1.5f, middle, x + width - 1.5f, middle, style);
  if ((segments & kDecimalPoint) != 0)
    DrawLedDot(display, x + width + 1.0f, bottom - 1.0f, style);
}

uint8_t SegmentsFor(uint32_t digit) { return kDigitSegments[digit % 10u]; }
}  // namespace

void TimingDashboard::set_stopwatch(pixoo::StopwatchSnapshot snapshot) {
  this->display_ms_ = snapshot.elapsed_ms;
  this->running_ = snapshot.running;
}

void TimingDashboard::set_timer(pixoo::TimerSnapshot snapshot) {
  if (snapshot.remaining_ms == 0) {
    this->display_ms_ = 0;
  } else {
    const uint32_t rounded_ms = (snapshot.remaining_ms + 9u) / 10u * 10u;
    this->display_ms_ = rounded_ms > kMaximumDisplayedMs
                            ? kMaximumDisplayedMs
                            : rounded_ms;
  }
  this->running_ = snapshot.running;
}

void TimingDashboard::Render(display::Display &display) const {
  display.fill(kBackground);

  const uint32_t elapsed = this->display_ms_;
  const uint32_t minutes = elapsed / 60000u;
  const uint32_t seconds = elapsed / 1000u % 60u;
  const uint32_t centiseconds = elapsed / 10u % 100u;

  constexpr float kMainY = 7.0f;
  constexpr float kMainWidth = 11.0f;
  constexpr float kMainHeight = 26.0f;
  DrawDigit(display, 3.0f, kMainY, kMainWidth, kMainHeight,
            SegmentsFor(minutes / 10u), kMainStyle);
  DrawDigit(display, 16.0f, kMainY, kMainWidth, kMainHeight,
            SegmentsFor(minutes), kMainStyle);
  DrawDigit(display, 36.0f, kMainY, kMainWidth, kMainHeight,
            SegmentsFor(seconds / 10u), kMainStyle);
  DrawDigit(display, 49.0f, kMainY, kMainWidth, kMainHeight,
            SegmentsFor(seconds), kMainStyle);

  if (this->running_) {
    DrawLedDot(display, 32.0f, 15.5f, kMainStyle);
    DrawLedDot(display, 32.0f, 25.5f, kMainStyle);
  } else {
    content::FillDisc(display, content::BlendCanvasOf(display),
                      content::Disc{32.0f, 15.5f, 1.15f},
                      kStoppedPunctuation);
    content::FillDisc(display, content::BlendCanvasOf(display),
                      content::Disc{32.0f, 25.5f, 1.15f},
                      kStoppedPunctuation);
  }

  DrawLedDot(display, 20.0f, 57.5f, kFractionStyle);
  DrawDigit(display, 24.5f, 41.0f, 8.0f, 18.0f,
            SegmentsFor(centiseconds / 10u), kFractionStyle);
  DrawDigit(display, 35.5f, 41.0f, 8.0f, 18.0f,
            SegmentsFor(centiseconds), kFractionStyle);
}

}  // namespace esphome::pixoo64::dashboard
