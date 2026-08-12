#pragma once

#include "app_state.h"
#include "dashboard/dashboard.h"

namespace esphome::pixoo64::dashboard {

// A standalone elapsed-time dashboard. ContentController supplies its snapshot
// through the typed composition route; it has no clock or ESPHome state.
class StopwatchDashboard final : public Dashboard {
 public:
  void set_stopwatch(pixoo::StopwatchSnapshot snapshot) {
    this->snapshot_ = snapshot;
  }
  bool available() const override { return true; }
  void Render(display::Display &display) const override;

 private:
  pixoo::StopwatchSnapshot snapshot_{};
};

}  // namespace esphome::pixoo64::dashboard
