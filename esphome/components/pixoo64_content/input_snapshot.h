#pragma once

#include <mutex>

namespace esphome::pixoo64::content {

// Copies small input values between the main task and the render owner. Drawing,
// socket operations and entity callbacks must run outside the copy lock.
template<typename T> class InputSnapshot {
 public:
  void Publish(const T &value) {
    std::lock_guard<std::mutex> lock(this->mutex_);
    this->value_ = value;
  }

  T Get() const {
    std::lock_guard<std::mutex> lock(this->mutex_);
    return this->value_;
  }

 private:
  mutable std::mutex mutex_;
  T value_{};
};

}  // namespace esphome::pixoo64::content
