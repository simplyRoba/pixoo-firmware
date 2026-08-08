#pragma once

#include <cstdint>

#include "esphome/components/display/display.h"
#include "esphome/components/font/font.h"
#include "app_state.h"

namespace esphome::pixoo64::content {

// Draws a transient notification as a top banner over already-rendered base
// content. The banner is a 1px severity-coloured border with a single line of
// text; text that fits is static, longer text scrolls horizontally so the whole
// message is readable. The caller renders the base first, then overlays this.
class NotificationRenderer {
 public:
  void set_font(font::Font *font) { this->font_ = font; }
  bool ready() const { return this->font_ != nullptr; }

  // Total banner height in pixels: 1px border + 1px pad + 8px text + 1px pad +
  // 1px border.
  static constexpr int kHeight = 12;

  // Draws the banner for `note` at its visible elapsed time, which drives the
  // scroll offset independently of the device's absolute clock.
  void Render(display::Display &display, const pixoo::Notification &note,
              uint32_t visible_elapsed_ms) const;

  // Milliseconds for the text to scroll one full pass at the current font, or 0
  // if the text fits statically (no scroll needed). The controller uses this to
  // hold a long notification until at least one pass completes. Display-free so
  // it can be computed when a notification starts.
  uint32_t ScrollPassMs(const pixoo::Notification &note) const;

 protected:
  int TextWidth(const char *text) const;

  font::Font *font_{nullptr};
};

}  // namespace esphome::pixoo64::content
