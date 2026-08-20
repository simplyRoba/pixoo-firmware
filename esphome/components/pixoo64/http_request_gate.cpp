#include "http_request_gate.h"

#include "esphome/core/log.h"

namespace esphome::pixoo64::adapters {
namespace {
const char *const TAG = "pixoo64.http_gate";
}

HttpRequestGate::~HttpRequestGate() {
  this->ready_.store(false, std::memory_order_release);
  if (this->mutex_ != nullptr) {
    vSemaphoreDelete(this->mutex_);
    this->mutex_ = nullptr;
  }
}

void HttpRequestGate::setup() {
  if (this->setup_attempted_)
    return;
  this->setup_attempted_ = true;
  this->mutex_ = xSemaphoreCreateMutex();
  if (this->mutex_ == nullptr) {
    ESP_LOGE(TAG, "HTTP request gate initialization failed");
    this->mark_failed();
    return;
  }
  this->ready_.store(true, std::memory_order_release);
}

void HttpRequestGate::Lease::Release() {
  if (this->gate_ != nullptr) {
    xSemaphoreGive(this->gate_->mutex_);
    this->gate_ = nullptr;
  }
}

}  // namespace esphome::pixoo64::adapters
