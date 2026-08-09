#pragma once

#include <cstdint>

#include "esphome/components/display/display.h"
#include "esphome/components/font/font.h"
#include "app_state.h"

namespace esphome::pixoo64::content {

// Draws a transient notification as a top banner over already-rendered base
// content. An untitled notification has the existing single message line; a
// titled notification uses title and message lines. Text that does not fit
// scrolls horizontally so each line is readable. The caller renders the base
// first, then overlays this.
class NotificationRenderer {
 public:
  void set_font(font::Font *font) { this->font_ = font; }
  bool ready() const { return this->font_ != nullptr; }

  // Untitled banner height: 1px border + 1px pad + 8px text + 1px pad + 1px
  // border. Titled banners add a second 8px line.
  static constexpr int kHeight = 12;
  static constexpr int kTitledHeight = 20;

  static int Height(const pixoo::Notification &note) {
    return note.title.empty() ? kHeight : kTitledHeight;
  }

  // Draws the banner for `note` at its visible elapsed time, which drives the
  // scroll offset independently of the device's absolute clock.
  void Render(display::Display &display, const pixoo::Notification &note,
              uint32_t visible_elapsed_ms) const;

  // Milliseconds for the longest text line to scroll one full pass at the
  // current font, or 0 if every line fits statically. The controller uses this
  // to hold a long notification until at least one pass completes. Display-free
  // so it can be computed when a notification starts.
  uint32_t ScrollPassMs(const pixoo::Notification &note) const;

 protected:
  int TextWidth(const char *text) const;
  uint32_t TextScrollPassMs_(const char *text) const;
  void RenderTextLine_(display::Display &display, const char *text, int top,
                       Color color, uint32_t visible_elapsed_ms) const;

  font::Font *font_{nullptr};
};

}  // namespace esphome::pixoo64::content
