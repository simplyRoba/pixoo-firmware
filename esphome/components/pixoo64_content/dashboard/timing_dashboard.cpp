#include "timing_dashboard.h"

#include <cstdint>

#include "digital_clock.h"
#include "esphome/components/pixoo64_content/aa_draw.h"
#include "esphome/components/pixoo64_content/blend_canvas.h"
#include "seven_segment.h"

namespace esphome::pixoo64::dashboard {
namespace {

using seven_segment::DrawDigit;
using seven_segment::DrawLedDot;
using seven_segment::LedStyle;

constexpr uint32_t kMaximumDisplayedMs =
    99u * 60u * 1000u + 59u * 1000u + 990u;

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

uint8_t SegmentsFor(uint32_t digit) {
  return pixoo::clock::DigitalSegmentsFor(static_cast<int>(digit % 10u));
}

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
