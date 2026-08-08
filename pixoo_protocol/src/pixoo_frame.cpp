#include "pixoo_frame.h"

#include <cstring>
#include <limits>

namespace pixoo {
namespace {

bool FrameSize(std::size_t payload_len, std::size_t* frame_len) {
  if (payload_len > std::numeric_limits<uint16_t>::max() ||
      payload_len > std::numeric_limits<std::size_t>::max() -
                        kWireFrameOverhead)
    return false;
  *frame_len = payload_len + kWireFrameOverhead;
  return true;
}

}  // namespace

std::size_t EncodeFrameTo(Cmd cmd, const uint8_t* payload,
                          std::size_t payload_len, uint8_t* out,
                          std::size_t out_capacity) {
  std::size_t frame_len = 0;
  if ((payload_len != 0 && payload == nullptr) || out == nullptr ||
      !FrameSize(payload_len, &frame_len) || out_capacity < frame_len)
    return 0;

  // Move before framing so rgb at out + 4, or any overlapping source, remains
  // well-defined and intact while the header is written.
  if (payload_len != 0) std::memmove(out + 4, payload, payload_len);
  out[0] = kFrameHeader;
  out[1] = static_cast<uint8_t>(payload_len & 0xFF);
  out[2] = static_cast<uint8_t>((payload_len >> 8) & 0xFF);
  out[3] = static_cast<uint8_t>(cmd);
  out[frame_len - 1] = kFrameTail;
  return frame_len;
}

std::size_t EncodeFullFrameTo(const uint8_t* rgb, std::size_t rgb_len,
                              uint8_t* out, std::size_t out_capacity) {
  if (rgb == nullptr ||
      rgb_len != static_cast<std::size_t>(kFramePayloadBytes))
    return 0;
  return EncodeFrameTo(Cmd::kFullFrameRgb, rgb, rgb_len, out, out_capacity);
}

std::size_t EncodeContinuationTo(std::size_t frame_len, bool is_full_frame,
                                 uint8_t* out, std::size_t out_capacity) {
  const std::size_t target =
      is_full_frame ? kFullFrameDmaTarget : kControlDmaTarget;
  if (frame_len >= target) return 0;
  const std::size_t continuation_len = target - frame_len;
  if (continuation_len < kMinFrameBytes || out == nullptr ||
      out_capacity < continuation_len)
    return 0;

  const std::size_t payload_len = continuation_len - kWireFrameOverhead;
  if (payload_len > std::numeric_limits<uint16_t>::max()) return 0;
  out[0] = kFrameHeader;
  out[1] = static_cast<uint8_t>(payload_len & 0xFF);
  out[2] = static_cast<uint8_t>((payload_len >> 8) & 0xFF);
  out[3] = static_cast<uint8_t>(Cmd::kContinuation);
  std::memset(out + 4, 0, payload_len);
  out[continuation_len - 1] = kFrameTail;
  return continuation_len;
}

std::vector<uint8_t> EncodeFrame(Cmd cmd, const uint8_t* payload,
                                 std::size_t payload_len) {
  std::size_t frame_len = 0;
  if (!FrameSize(payload_len, &frame_len)) return {};
  std::vector<uint8_t> out(frame_len);
  if (EncodeFrameTo(cmd, payload, payload_len, out.data(), out.size()) == 0)
    return {};
  return out;
}

std::vector<uint8_t> EncodeInit() {
  const uint8_t payload[1] = {0x00};
  return EncodeFrame(Cmd::kInit, payload, sizeof(payload));
}

std::vector<uint8_t> EncodeWhiteBalance(uint8_t r, uint8_t g, uint8_t b) {
  const uint8_t payload[3] = {r, g, b};
  return EncodeFrame(Cmd::kWhiteBalance, payload, sizeof(payload));
}

std::vector<uint8_t> EncodeFullFrame(const uint8_t* rgb, std::size_t rgb_len) {
  if (rgb == nullptr ||
      rgb_len != static_cast<std::size_t>(kFramePayloadBytes))
    return {};
  std::vector<uint8_t> out(rgb_len + kWireFrameOverhead);
  if (EncodeFullFrameTo(rgb, rgb_len, out.data(), out.size()) == 0) return {};
  return out;
}

std::vector<uint8_t> EncodeContinuation(std::size_t frame_len,
                                        bool is_full_frame) {
  const std::size_t target =
      is_full_frame ? kFullFrameDmaTarget : kControlDmaTarget;
  if (frame_len >= target) return {};
  const std::size_t continuation_len = target - frame_len;
  std::vector<uint8_t> out(continuation_len);
  if (EncodeContinuationTo(frame_len, is_full_frame, out.data(), out.size()) ==
      0)
    return {};
  return out;
}

}  // namespace pixoo
