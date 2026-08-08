#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>

#include "pixoo_cmd.h"

namespace pixoo {

// Scales into transmit and decides whether to send: on change, or once
// min_interval_ms elapsed. No clock/hardware access; the caller passes now_ms.
class FrameOutput {
 public:
  // Allocates retained previous-frame storage for allocation-free raw Prepare.
  // Raw input larger than this configured capacity is rejected.
  bool ConfigureCapacity(size_t capacity);

  // The caller owns transmit storage. This path performs no dynamic allocation
  // after ConfigureCapacity and accepts payloads through the configured size.
  bool Prepare(const uint8_t *framebuffer, size_t size, float brightness,
               uint32_t now_ms, uint32_t min_interval_ms, uint8_t *transmit,
               size_t transmit_capacity, bool force = false);
  void Reset();

  size_t RetainedCapacity() const { return this->retained_capacity_; }

 private:
  bool RememberIfDue_(const uint8_t *scaled, size_t size, uint32_t now_ms,
                      uint32_t min_interval_ms, bool force);

  std::unique_ptr<uint8_t[]> last_transmit_;
  size_t retained_capacity_{0};
  size_t raw_capacity_{0};
  size_t last_transmit_size_{0};
  uint32_t last_send_ms_{0};
  bool configured_{false};
  bool sent_once_{false};
};

}  // namespace pixoo
