#pragma once

#include "app_state.h"
#include "dashboard/dashboard.h"

namespace esphome::pixoo64::dashboard {

// A standalone timing dashboard. ContentController binds each configured face
// to its application-owned snapshot; it has no clock or ESPHome state.
class TimingDashboard final : public Dashboard {
 public:
  void set_stopwatch(pixoo::StopwatchSnapshot snapshot);
  void set_timer(pixoo::TimerSnapshot snapshot);
  bool available() const override { return true; }
  void Render(display::Display &display) const override;

 private:
  uint32_t display_ms_{0};
  bool running_{false};
};

}  // namespace esphome::pixoo64::dashboard
