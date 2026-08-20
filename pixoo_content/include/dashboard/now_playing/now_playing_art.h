#pragma once

#include <cstddef>
#include <cstdint>

namespace pixoo::now_playing {

constexpr size_t kArtworkWidth = 64;
constexpr size_t kArtworkHeight = 64;
constexpr size_t kArtworkPixelCount = kArtworkWidth * kArtworkHeight;
constexpr size_t kArtworkRgb565Bytes =
    kArtworkPixelCount * sizeof(uint16_t);

uint16_t Rgb565(uint8_t red, uint8_t green, uint8_t blue);

// Generates the same full-frame dark record motif for an identity on every
// call. The destination is caller-owned and the function allocates no memory.
bool GenerateArtworkPlaceholder(uint64_t identity, uint16_t *destination,
                                size_t destination_count);

}  // namespace pixoo::now_playing
