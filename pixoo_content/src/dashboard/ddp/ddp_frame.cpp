#include "ddp_frame.h"

#include <algorithm>

namespace pixoo::ddp {

void FrameBuffer::Reset() {
  assembly_.fill(0);
  published_.fill(0);
  has_frame_ = false;
}

DatagramResult FrameBuffer::Apply(const uint8_t *data, size_t size) {
  if (data == nullptr || size < kHeaderBytes || size > kMaxDatagramBytes) {
    return DatagramResult::kRejected;
  }
  if ((data[0] != 0x40 && data[0] != 0x41) || (data[1] & 0xF0) != 0 ||
      (data[2] != 0x0B && data[2] != 0x01) || data[3] != 1) {
    return DatagramResult::kRejected;
  }
  const uint32_t offset = (uint32_t{data[4]} << 24) |
                          (uint32_t{data[5]} << 16) |
                          (uint32_t{data[6]} << 8) | uint32_t{data[7]};
  const size_t length = (size_t{data[8]} << 8) | size_t{data[9]};
  if (length > kMaxPayloadBytes || size != kHeaderBytes + length ||
      offset > kFrameBytes || length > kFrameBytes - offset) {
    return DatagramResult::kRejected;
  }

  std::copy_n(data + kHeaderBytes, length, assembly_.begin() + offset);
  if (data[0] == 0x41) {
    published_ = assembly_;
    has_frame_ = true;
    return DatagramResult::kPublished;
  }
  return DatagramResult::kAccepted;
}

}  // namespace pixoo::ddp
