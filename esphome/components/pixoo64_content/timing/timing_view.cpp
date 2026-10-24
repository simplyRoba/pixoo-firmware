#include "timing_view.h"

#include <algorithm>
#include <cstdint>

#include "digital_clock.h"
#include "esphome/components/pixoo64_content/aa_draw.h"
#include "esphome/components/pixoo64_content/blend_canvas.h"
#include "../dashboard/seven_segment.h"
#include "../notification/banner.h"

namespace esphome::pixoo64::content {
namespace {

using seven_segment::DrawDigit;
using seven_segment::DrawLedDot;
using seven_segment::LedStyle;

constexpr uint32_t kMaximumDisplayedMs =
    99u * 60u * 1000u + 59u * 1000u + 990u;
const Color kBackground(2, 0, 0);
const LedStyle kMainStyle{1.35f, 2.45f, 0.50f,
                          Color(154, 10, 3), Color(238, 48, 25), Color(255, 154, 94)};
const LedStyle kFractionStyle{1.00f, 1.80f, 0.38f,
                              Color(146, 48, 3), Color(255, 146, 38), Color(255, 224, 146)};
const Color kStoppedPunctuation(83, 19, 12);

uint8_t SegmentsFor(uint32_t digit) {
  return pixoo::clock::DigitalSegmentsFor(static_cast<int>(digit % 10u));
}

}  // namespace

void TimingView::RenderBanner(display::Display &display,
                              const pixoo::TimingSnapshot &timing,
                              font::Font *font) const {
  if (font == nullptr)
    return;
  // Each row stores the icon's leftmost pixel in its highest used bit.
  static constexpr uint16_t kHourglass[9] = {
      0x7f, 0x22, 0x22, 0x14, 0x08, 0x14, 0x2a, 0x3e, 0x7f};
  static constexpr uint16_t kStopwatch[9] = {
      0x038, 0x012, 0x07c, 0x082, 0x111, 0x119, 0x101, 0x082, 0x07c};
  const bool timer = timing.kind == pixoo::TimingKind::kTimer;
  const bool paused = timing.phase == pixoo::TimingPhase::kPaused;
  const Color accent = timer ? Color(255, 170, 0) : Color(0, 153, 255);
  banner::DrawBackdrop(display, banner::kHeight, accent, true);
  const Color icon = paused
      ? Color((accent.r * 7 + 5) / 10, (accent.g * 7 + 5) / 10,
              (accent.b * 7 + 5) / 10)
      : accent;
  const int width = timer ? 7 : 9;
  const int left = timer ? 4 : 3;
  const uint16_t *rows = timer ? kHourglass : kStopwatch;
  for (int y = 0; y < 9; ++y)
    for (int x = 0; x < width; ++x)
      if (rows[y] & (1u << (width - 1 - x)))
        display.draw_pixel_at(left + x, 2 + y, icon);

  uint32_t seconds = timing.value_ms / 1000u;
  if (timer && timing.value_ms % 1000u != 0)
    ++seconds;
  seconds = std::min<uint32_t>(seconds, 99u * 60u + 59u);
  const uint32_t minutes = seconds / 60u;
  seconds %= 60u;
  const char label[] = {
      static_cast<char>('0' + minutes / 10u),
      static_cast<char>('0' + minutes % 10u), ':',
      static_cast<char>('0' + seconds / 10u),
      static_cast<char>('0' + seconds % 10u), '\0'};
  display.print(22, 2, font,
                paused ? Color(190, 194, 203) : Color(235, 238, 245),
                display::TextAlign::TOP_LEFT, label);
  if (paused) {
    display.filled_rectangle(58, 4, 2, 5, Color(235, 238, 245));
    display.filled_rectangle(61, 4, 2, 5, Color(235, 238, 245));
  }
}

void TimingView::Render(display::Display &display,
                        const pixoo::TimingSnapshot &timing,
                        font::Font *label_font) const {
  const bool finished = timing.kind == pixoo::TimingKind::kTimer &&
                        timing.phase == pixoo::TimingPhase::kFinished;
  display.fill(finished ? Color(24, 4, 0) : kBackground);
  uint32_t value = std::min(timing.value_ms, kMaximumDisplayedMs);
  if (timing.kind == pixoo::TimingKind::kTimer && value != 0)
    value = std::min<uint32_t>((value + 9u) / 10u * 10u, kMaximumDisplayedMs);
  const uint32_t minutes = value / 60000u;
  const uint32_t seconds = value / 1000u % 60u;
  const uint32_t centiseconds = value / 10u % 100u;

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

  if (timing.running()) {
    DrawLedDot(display, 32.0f, 15.5f, kMainStyle);
    DrawLedDot(display, 32.0f, 25.5f, kMainStyle);
  } else {
    FillDisc(display, BlendCanvasOf(display),
             Disc{32.0f, 15.5f, 1.15f}, kStoppedPunctuation);
    FillDisc(display, BlendCanvasOf(display),
             Disc{32.0f, 25.5f, 1.15f}, kStoppedPunctuation);
  }

  if (finished && label_font != nullptr) {
    display.print(32, 47, label_font, Color(255, 208, 144),
                  display::TextAlign::TOP_CENTER, "TIME UP");
  } else {
    DrawLedDot(display, 20.0f, 57.5f, kFractionStyle);
    DrawDigit(display, 24.5f, 41.0f, 8.0f, 18.0f,
              SegmentsFor(centiseconds / 10u), kFractionStyle);
    DrawDigit(display, 35.5f, 41.0f, 8.0f, 18.0f,
              SegmentsFor(centiseconds), kFractionStyle);
  }
}

}  // namespace esphome::pixoo64::content
