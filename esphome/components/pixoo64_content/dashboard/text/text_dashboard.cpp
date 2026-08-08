#include "text_dashboard.h"

namespace esphome::pixoo64::dashboard {

void TextDashboard::Render(display::Display &display) const {
  display.fill(Color(0, 0, 0));
  display.printf(32, 32, this->font_, display::TextAlign::CENTER, "%s",
                 this->text_->state.c_str());
}

}  // namespace esphome::pixoo64::dashboard
