#pragma once

#include "esphome/components/select/select.h"
#include "esphome/components/time/real_time_clock.h"
#include "esphome/core/component.h"
#include "esphome/core/preferences.h"

namespace esphome::pixoo64 {

// Select entity whose options come from the canonical pixoo::TimezoneCatalog and
// whose chosen POSIX TZ string is applied to a time component. The catalog is
// the single owner of the labels, the POSIX mapping, and the default; this
// adapter only bridges the entity to it, so no option list or mapping is
// duplicated in YAML.
class TimezoneSelect : public select::Select, public Component {
 public:
  void set_time(time::RealTimeClock *rtc) { this->rtc_ = rtc; }
  void set_restore_value(bool restore) { this->restore_value_ = restore; }

  void setup() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::DATA; }

 protected:
  void control(size_t index) override;
  void apply_index_(size_t index);

  time::RealTimeClock *rtc_{nullptr};
  bool restore_value_{false};
  ESPPreferenceObject pref_{};
};

}  // namespace esphome::pixoo64
