#pragma once

#include <atomic>
#include <cstdint>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esphome/core/component.h"

namespace esphome::pixoo64::adapters {

// Serializes every use of ESPHome's singleton HTTP client. A lease starts
// before get() and ends only after the response container has been ended.
class HttpRequestGate : public Component {
 public:
  HttpRequestGate() = default;
  ~HttpRequestGate();
  HttpRequestGate(const HttpRequestGate &) = delete;
  HttpRequestGate &operator=(const HttpRequestGate &) = delete;

  void setup() override;
  float get_setup_priority() const override { return setup_priority::HARDWARE; }

  class Lease {
   public:
    Lease() = default;
    explicit Lease(HttpRequestGate *gate) : gate_(gate) {}
    ~Lease() { this->Release(); }
    Lease(const Lease &) = delete;
    Lease &operator=(const Lease &) = delete;
    Lease(Lease &&other) noexcept : gate_(other.gate_) { other.gate_ = nullptr; }
    Lease &operator=(Lease &&other) noexcept {
      if (this != &other) { this->Release(); this->gate_ = other.gate_; other.gate_ = nullptr; }
      return *this;
    }
    explicit operator bool() const { return this->gate_ != nullptr; }
    void Release();
   private:
    HttpRequestGate *gate_{nullptr};
  };

  // Cancellation is checked between short waits, so only workers ever wait for
  // another HTTP transaction. The callable must be safe from the worker task.
  template<typename Cancelled>
  Lease Acquire(Cancelled cancelled) {
    if (!this->ready_.load(std::memory_order_acquire))
      return {};
    while (!cancelled()) {
      if (xSemaphoreTake(this->mutex_, pdMS_TO_TICKS(kWaitMs)) == pdTRUE) {
        if (!cancelled()) return Lease(this);
        xSemaphoreGive(this->mutex_);
        return {};
      }
    }
    return {};
  }

 private:
  friend class Lease;
  static constexpr uint32_t kWaitMs = 50;
  SemaphoreHandle_t mutex_{nullptr};
  std::atomic<bool> ready_{false};
  bool setup_attempted_{false};
};

}  // namespace esphome::pixoo64::adapters
