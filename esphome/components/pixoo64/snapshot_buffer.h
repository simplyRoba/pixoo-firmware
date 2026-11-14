#pragma once

#include <new>
#include <type_traits>

#include "esp_heap_caps.h"
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
  static_assert(std::is_trivially_copyable<T>::value, "snapshot must be trivially copyable");

  SnapshotBuffer() = default;
  SnapshotBuffer(const SnapshotBuffer &) = delete;
  SnapshotBuffer &operator=(const SnapshotBuffer &) = delete;
  ~SnapshotBuffer() {
    if (this->value_ != nullptr) {
      this->value_->~T();
      heap_caps_free(this->value_);
    }
  }

  // Called on the producer's main task. Consumers see a default value until
  // the first publication; allocation never occurs in Publish() or Get().
  bool Initialize() {
    if (this->value_ != nullptr)
      return true;
    void *storage = heap_caps_malloc(sizeof(T), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (storage == nullptr)
      return false;
    T *value = new (storage) T{};
    taskENTER_CRITICAL(&this->mux_);
    this->value_ = value;
    taskEXIT_CRITICAL(&this->mux_);
    return true;
  }

  void Publish(const T &value) {
    taskENTER_CRITICAL(&this->mux_);
    if (this->value_ != nullptr) {
      *this->value_ = value;
      this->has_value_ = true;
    }
    taskEXIT_CRITICAL(&this->mux_);
  }

  bool has_value() const {
    taskENTER_CRITICAL(&this->mux_);
    const bool has = this->has_value_;
    taskEXIT_CRITICAL(&this->mux_);
    return has;
  }

  // Copies the value and observes its availability in the same critical section.
  bool Read(T *out) const {
    if (out == nullptr)
      return false;
    taskENTER_CRITICAL(&this->mux_);
    const bool has = this->has_value_;
    if (has)
      *out = *this->value_;
    taskEXIT_CRITICAL(&this->mux_);
    return has;
  }

  // Returns a default-constructed T if nothing has been published.
  T Get() const {
    T out{};
    this->Read(&out);
    return out;
  }

  void Clear() {
    taskENTER_CRITICAL(&this->mux_);
    this->has_value_ = false;
    taskEXIT_CRITICAL(&this->mux_);
  }

 private:
  mutable portMUX_TYPE mux_ = portMUX_INITIALIZER_UNLOCKED;
  T *value_{nullptr};
  bool has_value_{false};
};

}  // namespace esphome::pixoo64::adapters::async
