#pragma once

#include <vector>

#include "esphome/components/select/select.h"
#include "esphome/core/component.h"
#include "esphome/core/preferences.h"
#include "dashboard_restore.h"

namespace esphome::pixoo64 {

class DashboardSelect final : public select::Select, public Component {
 public:
  void setup() override;
  float get_setup_priority() const override { return setup_priority::HARDWARE; }
  void set_initial_option(const std::string &value) { initial_option_ = value; }
  void set_restore_value(bool value) { restore_value_ = value; }
  void set_legacy_options(const std::vector<std::string> &options) { legacy_options_ = options; }
 protected:
  void control(size_t index) override;
  std::string initial_option_;
  std::vector<std::string> legacy_options_;
  bool restore_value_{true};
  ESPPreferenceObject pref_;
};

}  // namespace esphome::pixoo64
