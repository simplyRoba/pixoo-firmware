#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace pixoo::ddp {

constexpr size_t kFrameWidth = 64;
constexpr size_t kFrameHeight = 64;
constexpr size_t kFrameBytes = kFrameWidth * kFrameHeight * 3;
constexpr size_t kHeaderBytes = 10;
constexpr size_t kMaxPayloadBytes = 1440;
constexpr size_t kMaxDatagramBytes = kHeaderBytes + kMaxPayloadBytes;

enum class DatagramResult { kRejected, kAccepted, kPublished };

class FrameBuffer {
 public:
  void Reset();
  DatagramResult Apply(const uint8_t *data, size_t size);
  const std::array<uint8_t, kFrameBytes> &frame() const { return published_; }
  bool has_frame() const { return has_frame_; }

 private:
  std::array<uint8_t, kFrameBytes> assembly_{};
  std::array<uint8_t, kFrameBytes> published_{};
  bool has_frame_{false};
};

}  // namespace pixoo::ddp
