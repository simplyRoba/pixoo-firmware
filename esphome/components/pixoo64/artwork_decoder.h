#pragma once

#include <cstddef>
#include <cstdint>

namespace esphome::pixoo64::artwork {

constexpr size_t kArtworkWidth = 64;
constexpr size_t kArtworkHeight = 64;
constexpr size_t kArtworkPixelCount = kArtworkWidth * kArtworkHeight;
constexpr size_t kArtworkRgb565Bytes = kArtworkPixelCount * sizeof(uint16_t);
constexpr size_t kMaxEncodedBytes = 512 * 1024;
constexpr uint32_t kMaxSourceWidth = 4096;
constexpr uint32_t kMaxSourceHeight = 4096;
constexpr uint32_t kMaxSourcePixels = 16'777'216;
constexpr uint32_t kMaxDecodeCallbacks = kMaxSourcePixels;
constexpr uint32_t kMaxDecodeCallbackPixels = kMaxSourcePixels;
constexpr uint32_t kMaxPngChunks = 4096;
constexpr uint32_t kMaxJpegMarkers = 4096;

static_assert(kMaxSourceWidth * uint64_t{kMaxSourceHeight} ==
                  kMaxSourcePixels,
              "source dimension and pixel limits must agree");
static_assert(kArtworkPixelCount == 4096, "artwork slot geometry");
static_assert(kMaxEncodedBytes == 512u * 1024u, "encoded artwork limit");

enum class ImageMagic : uint8_t {
  kUnknown,
  kPng,
  kJpeg,
  kGif,
  kWebp,
};

enum class DecodeStatus : uint8_t {
  kSuccess,
  kInvalidArgument,
  kUnsupportedFormat,
  kMalformed,
  kIncomplete,
  kEncodedTooLarge,
  kDimensionsTooLarge,
  kProgressiveJpeg,
  kAnimatedPng,
  kInterlacedPng,
  kOutOfMemory,
  kDecodeFailed,
  kWorkLimitExceeded,
  kCancelled,
};

using CancellationCallback = bool (*)(void *context);

struct ImageInfo {
  ImageMagic format{ImageMagic::kUnknown};
  uint32_t width{0};
  uint32_t height{0};
};

struct CropRect {
  uint32_t x{0};
  uint32_t y{0};
  uint32_t width{0};
  uint32_t height{0};
};

ImageMagic ClassifyMagic(const uint8_t *bytes, size_t size);

// These inspectors parse the complete container before a decoder library sees
// it. They reject unsupported modes, malformed lengths/markers, missing end
// markers, oversized dimensions, and multiplication overflow.
DecodeStatus InspectPng(const uint8_t *encoded, size_t encoded_size,
                        ImageInfo *info);
DecodeStatus InspectJpeg(const uint8_t *encoded, size_t encoded_size,
                         ImageInfo *info);
DecodeStatus InspectArtwork(const uint8_t *encoded, size_t encoded_size,
                            ImageInfo *info);

CropRect CenterCrop(uint32_t source_width, uint32_t source_height);

uint16_t Rgb888ToRgb565(uint8_t red, uint8_t green, uint8_t blue);

// Decodes through a fixed 64x64 area-accumulation buffer and publishes to the
// caller's RGB565 slot only after complete, validated finalization. Inspection
// always completes before cancellation is observed.
DecodeStatus DecodeArtwork(const uint8_t *encoded, size_t encoded_size,
                           uint16_t *destination, size_t destination_count,
                           ImageInfo *decoded_info = nullptr,
                           CancellationCallback cancellation = nullptr,
                           void *cancellation_context = nullptr);

}  // namespace esphome::pixoo64::artwork
