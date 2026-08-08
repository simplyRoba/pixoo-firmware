#pragma once

#include "dashboard/dashboard.h"
#include "esphome/components/display/display.h"
#include "esphome/components/font/font.h"
#include "esphome/components/text/text.h"

namespace esphome::pixoo64::dashboard {

class TextDashboard : public Dashboard {
 public:
  void set_font(font::Font *font) { this->font_ = font; }
  void set_text(text::Text *text) { this->text_ = text; }
  bool available() const { return this->font_ != nullptr && this->text_ != nullptr; }
  void Render(display::Display &display) const;

 protected:
  font::Font *font_{nullptr};
  text::Text *text_{nullptr};
};

}  // namespace esphome::pixoo64::dashboard
