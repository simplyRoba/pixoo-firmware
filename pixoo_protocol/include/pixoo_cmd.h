// Panel SPI command set and frame geometry. See docs/hardware.md §5.
#pragma once
#include <cstddef>
#include <cstdint>

namespace pixoo {

constexpr uint8_t kFrameHeader = 0xAA;
constexpr uint8_t kFrameTail = 0xBB;

enum class Cmd : uint8_t {
  kFullFrameRgb = 0x00,   // payload = 12288 bytes (64*64*3)
  kBrightness = 0x01,     // 1-byte brightness value
  kInit = 0x10,           // 1-byte payload 0x00
  kContinuation = 0x21,
  kWhiteBalance = 0x22,   // 3-byte RGB gains
};

// Observed panel traffic pads each transaction with a 0x21 frame up to a DMA
// target, skipping the pad when fewer than kMinFrameBytes remain.
constexpr std::size_t kWireFrameOverhead = 5;  // AA len16 cmd BB
constexpr std::size_t kFullFrameDmaTarget = 0x30F5;  // 12533
constexpr std::size_t kControlDmaTarget = 240;
constexpr std::size_t kMinFrameBytes = 6;  // AA len16 cmd <1 byte> BB

constexpr int kWidth = 64;
constexpr int kHeight = 64;
constexpr int kChannels = 3;
constexpr int kPixelCount = kWidth * kHeight;
constexpr int kFramePayloadBytes = kPixelCount * kChannels;  // 12288
constexpr std::size_t kFullFrameWireBytes =
    static_cast<std::size_t>(kFramePayloadBytes) + kWireFrameOverhead;
constexpr std::size_t kFullFrameContinuationBytes =
    kFullFrameDmaTarget - kFullFrameWireBytes;

static_assert(kWidth == 64 && kHeight == 64 && kChannels == 3);
static_assert(kFramePayloadBytes == 12288);
static_assert(kFullFrameWireBytes == 12293);
static_assert(kFullFrameContinuationBytes == 240);
static_assert(kFullFrameDmaTarget ==
              kFullFrameWireBytes + kFullFrameContinuationBytes);
static_assert(kFullFrameWireBytes <= UINT16_MAX + kWireFrameOverhead);

}  // namespace pixoo
