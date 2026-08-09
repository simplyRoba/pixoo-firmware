#pragma once

#include <cstddef>
#include <cstdint>

#include "dashboard/dashboard.h"
#include "esphome/components/display/display.h"
#include "esphome/components/font/font.h"
#include "esphome/components/text/text.h"

namespace esphome::pixoo64::dashboard {

class TextDashboard : public Dashboard {
 public:
  void set_font(font::Font *font) { this->font_ = font; }
  void set_text(text::Text *text) { this->text_ = text; }
  bool available() const override {
    return this->font_ != nullptr && this->text_ != nullptr;
  }
  void OnShow(uint32_t now_ms) override;
  void Tick(uint32_t now_ms) override;
  void Render(display::Display &display) const override;

 protected:
  static constexpr size_t kMaximumTextBytes = 128;
  static constexpr size_t kMaximumLines = kMaximumTextBytes + 1;
  static constexpr int kContentLeft = 2;
  static constexpr int kContentRight = 62;
  static constexpr int kContentWidth = kContentRight - kContentLeft;
  static constexpr int kPanelHeight = 64;
  static constexpr size_t kMaximumLinesPerPage = 6;
  static constexpr uint32_t kPageDurationMs = 3000;
  static constexpr int kScrollPixelsPerSecond = 18;

  struct Line {
    uint8_t start;
    uint8_t length;
    int width;
    bool scrolls;
  };

  bool CopyText_();
  void LayoutText_();
  void LayoutParagraph_(size_t start, size_t end);
  void AddLine_(size_t start, size_t length, bool scrolls);
  void BuildPageDurations_();
  static uint32_t ScrollDurationMs_(int width);
  void CopyRange_(size_t start, size_t length) const;
  int TextWidth_(size_t start, size_t length) const;
  int BufferWidth_() const;
  int LineHeight_() const;
  size_t LinesPerPage_() const;
  static bool IsSpace_(char character);
  static size_t Utf8SequenceLength_(const char *data, size_t available);

  font::Font *font_{nullptr};
  text::Text *text_{nullptr};
  char content_[kMaximumTextBytes + 1]{};
  mutable char render_buffer_[kMaximumTextBytes + 1]{};
  Line lines_[kMaximumLines]{};
  size_t content_length_{0};
  size_t line_count_{0};
  size_t page_count_{0};
  uint32_t page_durations_ms_[kMaximumLines]{};
  uint64_t page_cycle_duration_ms_{0};
  uint32_t page_started_ms_{0};
  uint32_t current_ms_{0};
  bool short_text_{false};
};

}  // namespace esphome::pixoo64::dashboard
