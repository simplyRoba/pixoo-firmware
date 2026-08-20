#include "now_playing_art.h"

#include <algorithm>

namespace pixoo::now_playing {
namespace {

uint64_t SplitMix64(uint64_t value) {
  value += 0x9e3779b97f4a7c15ull;
  value = (value ^ (value >> 30)) * 0xbf58476d1ce4e5b9ull;
  value = (value ^ (value >> 27)) * 0x94d049bb133111ebull;
  return value ^ (value >> 31);
}

}  // namespace

uint16_t Rgb565(uint8_t red, uint8_t green, uint8_t blue) {
  return static_cast<uint16_t>((static_cast<uint16_t>(red >> 3) << 11) |
                               (static_cast<uint16_t>(green >> 2) << 5) |
                               static_cast<uint16_t>(blue >> 3));
}

bool GenerateArtworkPlaceholder(uint64_t identity, uint16_t *destination,
                                size_t destination_count) {
  if (destination == nullptr || destination_count < kArtworkPixelCount)
    return false;
  const uint64_t seed = SplitMix64(identity ^ 0x415254504c414345ull);
  const uint8_t tint_red = static_cast<uint8_t>(10 + ((seed >> 0) & 0x0f));
  const uint8_t tint_green = static_cast<uint8_t>(14 + ((seed >> 8) & 0x17));
  const uint8_t tint_blue = static_cast<uint8_t>(22 + ((seed >> 16) & 0x1f));
  const uint8_t accent_red = static_cast<uint8_t>(72 + ((seed >> 24) & 0x3f));
  const uint8_t accent_green = static_cast<uint8_t>(92 + ((seed >> 32) & 0x4f));
  const uint8_t accent_blue = static_cast<uint8_t>(116 + ((seed >> 40) & 0x5f));

  for (int y = 0; y < static_cast<int>(kArtworkHeight); ++y) {
    for (int x = 0; x < static_cast<int>(kArtworkWidth); ++x) {
      const int dx = x - 30;
      const int dy = y - 31;
      const int radius_squared = dx * dx + dy * dy;
      const int edge =
          std::min(std::min(x, 63 - x), std::min(y, 63 - y));
      const uint8_t gradient = static_cast<uint8_t>((x + 2 * y) / 12);
      uint8_t red = static_cast<uint8_t>(tint_red + gradient + edge / 10);
      uint8_t green = static_cast<uint8_t>(tint_green + gradient + edge / 8);
      uint8_t blue = static_cast<uint8_t>(tint_blue + gradient + edge / 6);
      if (radius_squared <= 20 * 20) {
        const uint8_t ring = static_cast<uint8_t>((radius_squared / 19) & 3);
        red = static_cast<uint8_t>(18 + tint_red / 2 + ring * 3);
        green = static_cast<uint8_t>(20 + tint_green / 2 + ring * 3);
        blue = static_cast<uint8_t>(28 + tint_blue / 2 + ring * 4);
      }
      if (radius_squared <= 4 * 4 ||
          ((x == 47 || x == 48) && y >= 17 && y <= 38) ||
          (y >= 16 && y <= 19 && x >= 40 && x <= 48) ||
          (y >= 36 && y <= 40 && x >= 43 && x <= 48)) {
        red = accent_red;
        green = accent_green;
        blue = accent_blue;
      }
      destination[y * kArtworkWidth + x] = Rgb565(red, green, blue);
    }
  }
  return true;
}

}  // namespace pixoo::now_playing
