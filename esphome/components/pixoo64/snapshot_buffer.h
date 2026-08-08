#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace esphome::pixoo64::adapters::async {

// Single-producer / single-consumer handoff of a plain-copyable value across
// tasks. The producer calls Publish() with a completed value; the consumer
// calls Get() for a coherent snapshot. A short critical section guards the copy,
// so the consumer never reads a torn multi-field struct. T must be plainly
// copyable (no heap ownership).
template<typename T>
class SnapshotBuffer {
 public:
  void Publish(const T &value) {
    taskENTER_CRITICAL(&this->mux_);
    this->value_ = value;
    this->has_value_ = true;
    taskEXIT_CRITICAL(&this->mux_);
  }

  bool has_value() const {
    taskENTER_CRITICAL(&this->mux_);
    const bool has = this->has_value_;
    taskEXIT_CRITICAL(&this->mux_);
    return has;
  }

  // Returns the latest published value by copy. If nothing has been published,
  // returns a default-constructed T.
  T Get() const {
    T out{};
    taskENTER_CRITICAL(&this->mux_);
    if (this->has_value_)
      out = this->value_;
    taskEXIT_CRITICAL(&this->mux_);
    return out;
  }

  void Clear() {
    taskENTER_CRITICAL(&this->mux_);
    this->has_value_ = false;
    taskEXIT_CRITICAL(&this->mux_);
  }

 private:
  mutable portMUX_TYPE mux_ = portMUX_INITIALIZER_UNLOCKED;
  T value_{};
  bool has_value_{false};
};

}  // namespace esphome::pixoo64::adapters::async
