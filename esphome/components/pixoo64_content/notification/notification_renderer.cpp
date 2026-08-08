#include "notification_renderer.h"

namespace esphome::pixoo64::content {
namespace {

const Color kBg(0, 0, 0);
const Color kText(235, 238, 245);

// Banner interior available for text, inside the 1px border + 1px padding.
constexpr int kInnerLeft = 2;
constexpr int kInnerRight = 62;  // exclusive-ish right edge for layout
constexpr int kInnerWidth = kInnerRight - kInnerLeft;  // 60px
constexpr int kTextTop = 2;

// Horizontal scroll speed for text that does not fit. Tuned on-panel for
// readability of an 8px line on the 64px display.
constexpr int kScrollPxPerSec = 18;

Color SeverityColor(pixoo::Severity severity) {
  switch (severity) {
    case pixoo::Severity::kSuccess:
      return Color(0x00, 0xCC, 0x44);
    case pixoo::Severity::kWarning:
      return Color(0xFF, 0xAA, 0x00);
    case pixoo::Severity::kError:
      return Color(0xFF, 0x44, 0x44);
    case pixoo::Severity::kInfo:
      break;
  }
  return Color(0x00, 0x99, 0xFF);
}

}  // namespace

int NotificationRenderer::TextWidth(const char *text) const {
  int w = 0, x_offset = 0, baseline = 0, height = 0;
  this->font_->measure(text, &w, &x_offset, &baseline, &height);
  return w;
}

uint32_t NotificationRenderer::ScrollPassMs(
    const pixoo::Notification &note) const {
  if (this->font_ == nullptr)
    return 0;
  const int text_w = this->TextWidth(note.text.c_str());
  if (text_w <= kInnerWidth)
    return 0;
  const int travel = kInnerWidth + text_w;
  return static_cast<uint32_t>(travel) * 1000u / kScrollPxPerSec;
}

void NotificationRenderer::Render(display::Display &display,
                                  const pixoo::Notification &note,
                                  uint32_t visible_elapsed_ms) const {
  if (this->font_ == nullptr)
    return;

  // Opaque banner background plus a severity-coloured 1px border.
  display.filled_rectangle(0, 0, 64, kHeight, kBg);
  display.rectangle(0, 0, 64, kHeight, SeverityColor(note.severity));

  const char *text = note.text.c_str();
  const int text_w = this->TextWidth(text);

  display.start_clipping(kInnerLeft, kTextTop, kInnerRight, kTextTop + 8);
  if (text_w <= kInnerWidth) {
    const int x = kInnerLeft + (kInnerWidth - text_w) / 2;
    display.print(x, kTextTop, this->font_, kText, display::TextAlign::TOP_LEFT,
                  text);
  } else {
    // Scroll right-to-left: the text enters from the right edge and travels
    // until it has fully left past the left edge, then repeats.
    const int travel = kInnerWidth + text_w;
    const int phase =
        static_cast<int>((visible_elapsed_ms / 1000u * kScrollPxPerSec +
                          visible_elapsed_ms % 1000u * kScrollPxPerSec / 1000u) %
                         static_cast<uint32_t>(travel));
    const int x = kInnerRight - phase;
    display.print(x, kTextTop, this->font_, kText, display::TextAlign::TOP_LEFT,
                  text);
  }
  display.end_clipping();
}

}  // namespace esphome::pixoo64::content
