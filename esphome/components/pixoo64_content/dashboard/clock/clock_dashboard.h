#pragma once

#include <cstdint>
#include <ctime>

#include "analog_face.h"
#include "binary_face.h"
#include "dashboard/dashboard.h"
#include "esphome/components/display/display.h"
#include "esphome/components/time/real_time_clock.h"
#include "split_flap_face.h"
#include "watch_face.h"

namespace esphome::pixoo64::dashboard {

using clockface::ClockTime;
using clockface::WatchFace;

// Owns clock resolution and hands the hour/minute plus the tick clock to one
// watch face, which owns layout and animation. The face is bound per dashboard
// instance at composition time, so each face is its own selectable dashboard.
class ClockDashboard : public Dashboard {
 public:
  void set_time(time::RealTimeClock *t) { this->time_ = t; }
  // Pins the clock to a fixed UTC epoch, taking precedence over the RTC.
  void set_fixed_time(time_t epoch) {
    this->fixed_epoch_ = epoch;
    this->has_fixed_time_ = true;
  }
  void set_face(WatchFace *face) { this->face_ = face; }

  bool available() const override { return this->face_ != nullptr; }

  void Tick(uint32_t now_ms) override {
    if (this->face_ != nullptr)
      this->face_->Tick(this->resolve_time_(), now_ms);
  }

  void OnShow(uint32_t now_ms) override {
    if (this->face_ != nullptr)
      this->face_->OnShow(now_ms);
  }

  void Render(display::Display &display) const override {
    this->face_->Render(display);
  }

 protected:
  ClockTime resolve_time_() const {
    ESPTime now{};
    if (this->has_fixed_time_)
      now = ESPTime::from_epoch_utc(this->fixed_epoch_);
    else if (this->time_ != nullptr)
      now = this->time_->now();
    else
      return ClockTime{};
    if (!now.is_valid())
      return ClockTime{};
    return ClockTime{now.hour, now.minute, now.second, true};
  }

  time::RealTimeClock *time_{nullptr};
  time_t fixed_epoch_{0};
  bool has_fixed_time_{false};
  WatchFace *face_{nullptr};
};

class SplitFlapClockDashboard : public ClockDashboard {
 public:
  SplitFlapClockDashboard() { this->set_face(&this->face_impl_); }

 protected:
  clockface::SplitFlapFace face_impl_;
};

class AnalogClockDashboard : public ClockDashboard {
 public:
  AnalogClockDashboard() { this->set_face(&this->face_impl_); }

 protected:
  clockface::AnalogFace face_impl_;
};

class BinaryClockDashboard : public ClockDashboard {
 public:
  BinaryClockDashboard() { this->set_face(&this->face_impl_); }

 protected:
  clockface::BinaryFace face_impl_;
};

}  // namespace esphome::pixoo64::dashboard
