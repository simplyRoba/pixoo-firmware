#include "pixoo_output.h"

#include <cmath>
#include <cstring>
#include <new>
#include <utility>

namespace pixoo {
namespace {

float ClampBrightness(float brightness) {
  if (!std::isfinite(brightness) || brightness < 0.0f) return 0.0f;
  if (brightness > 1.0f) return 1.0f;
  return brightness;
}

uint8_t Scale(uint8_t value, float brightness) {
  return static_cast<uint8_t>(value * brightness + 0.5f);
}

void ScaleInto(const uint8_t *framebuffer, size_t size, float brightness,
               uint8_t *transmit) {
  if (size == 0) return;

  const uintptr_t source = reinterpret_cast<uintptr_t>(framebuffer);
  const uintptr_t destination = reinterpret_cast<uintptr_t>(transmit);
  if (destination > source && destination - source < size) {
    for (size_t index = size; index != 0; --index)
      transmit[index - 1] = Scale(framebuffer[index - 1], brightness);
    return;
  }
  for (size_t index = 0; index < size; ++index)
    transmit[index] = Scale(framebuffer[index], brightness);
}

}  // namespace

bool FrameOutput::ConfigureCapacity(size_t capacity) {
  if (capacity > static_cast<size_t>(kFramePayloadBytes)) return false;
  std::unique_ptr<uint8_t[]> storage;
  if (capacity != 0) {
    storage.reset(new (std::nothrow) uint8_t[capacity]);
    if (!storage) return false;
  }
  this->last_transmit_ = std::move(storage);
  this->retained_capacity_ = capacity;
  this->raw_capacity_ = capacity;
  this->configured_ = true;
  this->Reset();
  return true;
}

bool FrameOutput::RememberIfDue_(const uint8_t *scaled, size_t size,
                                 uint32_t now_ms,
                                 uint32_t min_interval_ms, bool force) {
  const bool changed =
      !this->sent_once_ || size != this->last_transmit_size_ ||
      (size != 0 && std::memcmp(scaled, this->last_transmit_.get(), size) != 0);
  const bool interval_elapsed =
      this->sent_once_ && (now_ms - this->last_send_ms_) >= min_interval_ms;
  if (!force && !changed && !interval_elapsed) return false;

  if (size > this->retained_capacity_)
    return false;
  if (size != 0) std::memcpy(this->last_transmit_.get(), scaled, size);
  this->last_transmit_size_ = size;
  this->last_send_ms_ = now_ms;
  this->sent_once_ = true;
  return true;
}

bool FrameOutput::Prepare(const uint8_t *framebuffer, size_t size,
                          float brightness, uint32_t now_ms,
                          uint32_t min_interval_ms, uint8_t *transmit,
                          size_t transmit_capacity, bool force) {
  if (!this->configured_ || framebuffer == nullptr ||
      size > this->raw_capacity_ || transmit_capacity < size ||
      (size != 0 && transmit == nullptr))
    return false;

  ScaleInto(framebuffer, size, ClampBrightness(brightness), transmit);
  return this->RememberIfDue_(transmit, size, now_ms, min_interval_ms, force);
}

void FrameOutput::Reset() {
  this->last_transmit_size_ = 0;
  this->last_send_ms_ = 0;
  this->sent_once_ = false;
}

}  // namespace pixoo
