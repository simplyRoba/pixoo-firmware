// Frame encoder: 0xAA | len[LE16] | cmd | payload | 0xBB. See docs/hardware.md §5.
#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

#include "pixoo_cmd.h"

namespace pixoo {

// Writes 0xAA | len[LE16] | cmd | payload | 0xBB to out. Returns its byte
// count, or 0 when the payload, output buffer, or capacity is invalid.
std::size_t EncodeFrameTo(Cmd cmd, const uint8_t* payload,
                          std::size_t payload_len, uint8_t* out,
                          std::size_t out_capacity);

// Writes a full RGB frame. rgb may already point at out + 4. Returns the byte
// count, or 0 when the RGB size, output buffer, or capacity is invalid.
std::size_t EncodeFullFrameTo(const uint8_t* rgb, std::size_t rgb_len,
                              uint8_t* out, std::size_t out_capacity);

// Writes the 0x21 pad for a frame_len-byte frame. Returns its byte count, or
// 0 when no pad is due or the output capacity is insufficient.
std::size_t EncodeContinuationTo(std::size_t frame_len, bool is_full_frame,
                                 uint8_t* out, std::size_t out_capacity);

std::vector<uint8_t> EncodeFrame(Cmd cmd, const uint8_t* payload,
                                 std::size_t payload_len);

inline std::vector<uint8_t> EncodeFrame(Cmd cmd,
                                        const std::vector<uint8_t>& payload) {
  return EncodeFrame(cmd, payload.data(), payload.size());
}

std::vector<uint8_t> EncodeInit();
std::vector<uint8_t> EncodeWhiteBalance(uint8_t r, uint8_t g, uint8_t b);

// Returns empty if rgb_len != kFramePayloadBytes.
std::vector<uint8_t> EncodeFullFrame(const uint8_t* rgb, std::size_t rgb_len);

// Builds the 0x21 pad for a frame_len-byte frame. Empty if no pad is due.
std::vector<uint8_t> EncodeContinuation(std::size_t frame_len,
                                        bool is_full_frame);

}  // namespace pixoo
