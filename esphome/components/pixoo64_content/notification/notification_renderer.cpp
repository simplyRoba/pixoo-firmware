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
constexpr int kTitleMessageTop = 10;

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

uint32_t NotificationRenderer::TextScrollPassMs_(const char *text) const {
  const int text_w = this->TextWidth(text);
  if (text_w <= kInnerWidth)
    return 0;
  const int travel = kInnerWidth + text_w;
  return static_cast<uint32_t>(travel) * 1000u / kScrollPxPerSec;
}

uint32_t NotificationRenderer::ScrollPassMs(
    const pixoo::Notification &note) const {
  if (this->font_ == nullptr)
    return 0;
  const uint32_t message_pass = this->TextScrollPassMs_(note.text.c_str());
  if (note.title.empty())
    return message_pass;
  const uint32_t title_pass = this->TextScrollPassMs_(note.title.c_str());
  return title_pass > message_pass ? title_pass : message_pass;
}

void NotificationRenderer::RenderTextLine_(
    display::Display &display, const char *text, int top, Color color,
    uint32_t visible_elapsed_ms) const {
  const int text_w = this->TextWidth(text);
  display.start_clipping(kInnerLeft, top, kInnerRight, top + 8);
  if (text_w <= kInnerWidth) {
    const int x = kInnerLeft + (kInnerWidth - text_w) / 2;
    display.print(x, top, this->font_, color, display::TextAlign::TOP_LEFT,
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
    display.print(x, top, this->font_, color, display::TextAlign::TOP_LEFT,
                  text);
  }
  display.end_clipping();
}

void NotificationRenderer::Render(display::Display &display,
                                  const pixoo::Notification &note,
                                  uint32_t visible_elapsed_ms) const {
  if (this->font_ == nullptr)
    return;

  // Opaque banner background plus a severity-coloured 1px border.
  const Color severity_color = SeverityColor(note.severity);
  display.filled_rectangle(0, 0, 64, Height(note), kBg);
  display.rectangle(0, 0, 64, Height(note), severity_color);

  if (note.title.empty()) {
    this->RenderTextLine_(display, note.text.c_str(), kTextTop, kText,
                          visible_elapsed_ms);
    return;
  }
  this->RenderTextLine_(display, note.title.c_str(), kTextTop, severity_color,
                        visible_elapsed_ms);
  this->RenderTextLine_(display, note.text.c_str(), kTitleMessageTop, kText,
                        visible_elapsed_ms);
}

}  // namespace esphome::pixoo64::content
