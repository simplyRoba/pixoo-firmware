// Minimal deterministic PNG encoder for render tests: 8-bit truecolor RGB,
// filter none, single stored (uncompressed) deflate block. Identical pixels
// always produce identical bytes, so references compare by byte equality.
#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

namespace esphome::pixoo64_render_test {

// Encodes width*height*3 RGB bytes (row-major) to PNG file bytes.
std::vector<uint8_t> EncodePng(const uint8_t *rgb, int width, int height);

}  // namespace esphome::pixoo64_render_test
