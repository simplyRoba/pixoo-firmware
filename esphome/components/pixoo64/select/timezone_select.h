#pragma once

#include "esphome/components/select/select.h"
#include "esphome/components/time/real_time_clock.h"
#include "esphome/components/time/posix_tz.h"
#include "esphome/core/component.h"
#include "esphome/core/preferences.h"

namespace esphome::pixoo64 {

// Labels, persisted indexes, and pre-parsed timezone rules share one catalog.
class TimezoneSelect : public select::Select, public Component {
 public:
  void set_time(time::RealTimeClock *rtc) { this->rtc_ = rtc; }
  void set_restore_value(bool restore) { this->restore_value_ = restore; }
  void set_timezones(const time::ParsedTimezone *timezones, size_t count) {
    this->timezones_ = timezones;
    this->timezone_count_ = count;
  }

  void setup() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::DATA; }

 protected:
  void control(size_t index) override;
  void apply_index_(size_t index);

  time::RealTimeClock *rtc_{nullptr};
  const time::ParsedTimezone *timezones_{nullptr};
  size_t timezone_count_{0};
  bool restore_value_{false};
  ESPPreferenceObject pref_{};
};

}  // namespace esphome::pixoo64
