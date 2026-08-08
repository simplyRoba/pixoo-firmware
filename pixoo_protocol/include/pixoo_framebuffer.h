// 64x64 RGB framebuffer -> 12288-byte payload.
// Pixel order is row-major, R,G,B, byte 0 = pixel (0,0). PayloadIndex is the seam.
#pragma once
#include <array>
#include <cstdint>
#include <vector>

#include "pixoo_cmd.h"

namespace pixoo {

struct Rgb {
  uint8_t r;
  uint8_t g;
  uint8_t b;
  Rgb() : r(0), g(0), b(0) {}
  Rgb(uint8_t red, uint8_t green, uint8_t blue) : r(red), g(green), b(blue) {}
};

class Framebuffer {
 public:
  Framebuffer();

  static constexpr int kWidth = pixoo::kWidth;
  static constexpr int kHeight = pixoo::kHeight;

  void SetPixel(int x, int y, Rgb color);  // out-of-range ignored
  Rgb GetPixel(int x, int y) const;        // out-of-range -> black
  // The three payload bytes R,G,B of one pixel, or nullptr when out of range.
  // Compositing reads and writes the same pixel, which GetPixel plus SetPixel
  // reaches through two separate address computations. This range check is the
  // only thing keeping a caller's coordinate arithmetic inside the payload;
  // callers that bound their own loops do so for speed, not for safety.
  uint8_t *PixelBytes(int x, int y);
  const uint8_t *PixelBytes(int x, int y) const;
  void Fill(Rgb color);
  void Clear();
  std::vector<uint8_t> ToPayload() const;
  const std::array<uint8_t, kFramePayloadBytes> &payload() const {
    return data_;
  }

 private:
  static int PayloadIndex(int x, int y);
  std::array<uint8_t, kFramePayloadBytes> data_{};
};

}  // namespace pixoo
