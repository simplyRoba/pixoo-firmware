#ifdef USE_PIXOO64_NOW_PLAYING

#include "now_playing_adapter_test.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <chrono>
#include <thread>

#include <jpeglib.h>
#include "progressive_jpeg.h"
#include <string>
#include <type_traits>
#include <vector>

#include <unity.h>

extern "C" void setUp() {}
extern "C" void tearDown() {}

#include "now_playing_config.h"
#include "artwork_decoder.h"
#include "artwork_fetch_policy.h"
#include "http_body_policy.h"
#include "weather_fetch_policy.h"

namespace cfg = esphome::pixoo64::now_playing_config;
namespace artwork = esphome::pixoo64::artwork;

namespace {

void AppendBe16(std::vector<uint8_t> *bytes, uint16_t value) {
  bytes->push_back(static_cast<uint8_t>(value >> 8));
  bytes->push_back(static_cast<uint8_t>(value));
}

void AppendBe32(std::vector<uint8_t> *bytes, uint32_t value) {
  bytes->push_back(static_cast<uint8_t>(value >> 24));
  bytes->push_back(static_cast<uint8_t>(value >> 16));
  bytes->push_back(static_cast<uint8_t>(value >> 8));
  bytes->push_back(static_cast<uint8_t>(value));
}

uint32_t FixtureCrc32(const uint8_t *bytes, size_t size) {
  uint32_t crc = 0xffffffffu;
  for (size_t i = 0; i < size; ++i) {
    crc ^= bytes[i];
    for (uint8_t bit = 0; bit < 8; ++bit)
      crc = (crc >> 1) ^
            (0xedb88320u & (0u - static_cast<uint32_t>(crc & 1u)));
  }
  return crc ^ 0xffffffffu;
}

void AppendPngChunk(std::vector<uint8_t> *png, const char type[5],
                    const std::vector<uint8_t> &data) {
  AppendBe32(png, static_cast<uint32_t>(data.size()));
  const size_t crc_start = png->size();
  png->insert(png->end(), type, type + 4);
  png->insert(png->end(), data.begin(), data.end());
  AppendBe32(png, FixtureCrc32(png->data() + crc_start, data.size() + 4));
}

uint32_t FixtureAdler32(const std::vector<uint8_t> &bytes) {
  uint32_t a = 1;
  uint32_t b = 0;
  for (uint8_t value : bytes) {
    a = (a + value) % 65521u;
    b = (b + a) % 65521u;
  }
  return (b << 16) | a;
}

std::vector<uint8_t> StoredZlib(const std::vector<uint8_t> &raw) {
  std::vector<uint8_t> zlib{0x78, 0x01};
  size_t offset = 0;
  do {
    const size_t count = std::min<size_t>(65535, raw.size() - offset);
    const bool final = offset + count == raw.size();
    zlib.push_back(final ? 0x01 : 0x00);
    zlib.push_back(static_cast<uint8_t>(count));
    zlib.push_back(static_cast<uint8_t>(count >> 8));
    const uint16_t inverse = static_cast<uint16_t>(~count);
    zlib.push_back(static_cast<uint8_t>(inverse));
    zlib.push_back(static_cast<uint8_t>(inverse >> 8));
    zlib.insert(zlib.end(), raw.begin() + offset, raw.begin() + offset + count);
    offset += count;
  } while (offset < raw.size());
  AppendBe32(&zlib, FixtureAdler32(raw));
  return zlib;
}

std::vector<uint8_t> MakeRgbaPng(uint32_t width, uint32_t height,
                                 const std::vector<uint8_t> &rgba,
                                 bool interlaced = false,
                                 bool animated = false) {
  TEST_ASSERT_EQUAL_UINT64(static_cast<uint64_t>(width) * height * 4,
                           rgba.size());
  std::vector<uint8_t> png{0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a};
  std::vector<uint8_t> ihdr;
  AppendBe32(&ihdr, width);
  AppendBe32(&ihdr, height);
  ihdr.insert(ihdr.end(), {8, 6, 0, 0,
                           static_cast<uint8_t>(interlaced ? 1 : 0)});
  AppendPngChunk(&png, "IHDR", ihdr);
  if (animated) {
    std::vector<uint8_t> actl;
    AppendBe32(&actl, 1);
    AppendBe32(&actl, 0);
    AppendPngChunk(&png, "acTL", actl);
  }
  std::vector<uint8_t> raw;
  raw.reserve((static_cast<size_t>(width) * 4 + 1) * height);
  for (uint32_t y = 0; y < height; ++y) {
    raw.push_back(0);
    const size_t begin = static_cast<size_t>(y) * width * 4;
    raw.insert(raw.end(), rgba.begin() + begin,
               rgba.begin() + begin + width * 4);
  }
  AppendPngChunk(&png, "IDAT", StoredZlib(raw));
  AppendPngChunk(&png, "IEND", {});
  return png;
}

std::vector<uint8_t> MakeRgbPng(uint32_t width, uint32_t height,
                                const std::vector<uint8_t> &rgb,
                                bool transparent = false,
                                bool interlaced = false,
                                bool animated = false) {
  TEST_ASSERT_EQUAL_UINT64(static_cast<uint64_t>(width) * height * 3,
                           rgb.size());
  std::vector<uint8_t> png{0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a};
  std::vector<uint8_t> ihdr;
  AppendBe32(&ihdr, width);
  AppendBe32(&ihdr, height);
  ihdr.insert(ihdr.end(), {8, 2, 0, 0,
                           static_cast<uint8_t>(interlaced ? 1 : 0)});
  AppendPngChunk(&png, "IHDR", ihdr);
  if (animated) {
    std::vector<uint8_t> actl;
    AppendBe32(&actl, 1);
    AppendBe32(&actl, 0);
    AppendPngChunk(&png, "acTL", actl);
  }
  if (transparent)
    AppendPngChunk(&png, "tRNS", {0, 0, 0, 0, 0, 0});
  std::vector<uint8_t> raw;
  raw.reserve((static_cast<size_t>(width) * 3 + 1) * height);
  for (uint32_t y = 0; y < height; ++y) {
    raw.push_back(0);
    const size_t begin = static_cast<size_t>(y) * width * 3;
    raw.insert(raw.end(), rgb.begin() + begin, rgb.begin() + begin + width * 3);
  }
  AppendPngChunk(&png, "IDAT", StoredZlib(raw));
  AppendPngChunk(&png, "IEND", {});
  return png;
}

void AppendJpegSegment(std::vector<uint8_t> *jpeg, uint8_t marker,
                       const std::vector<uint8_t> &payload) {
  jpeg->push_back(0xff);
  jpeg->push_back(marker);
  AppendBe16(jpeg, static_cast<uint16_t>(payload.size() + 2));
  jpeg->insert(jpeg->end(), payload.begin(), payload.end());
}

struct HuffCode {
  uint16_t bits{0};
  uint8_t size{0};
};

std::array<HuffCode, 256> MakeHuffmanCodes(
    const std::array<uint8_t, 16> &counts,
    const std::vector<uint8_t> &values) {
  std::array<HuffCode, 256> result{};
  uint16_t code = 0;
  size_t value_index = 0;
  for (uint8_t length = 1; length <= 16; ++length) {
    for (uint8_t n = 0; n < counts[length - 1]; ++n) {
      result[values[value_index++]] = {code, length};
      ++code;
    }
    code <<= 1;
  }
  TEST_ASSERT_EQUAL_UINT(values.size(), value_index);
  return result;
}

class EntropyWriter {
 public:
  void Write(uint16_t bits, uint8_t count) {
    pending_ = (pending_ << count) | (bits & ((1u << count) - 1u));
    pending_count_ += count;
    while (pending_count_ >= 8) {
      const uint8_t byte = static_cast<uint8_t>(
          pending_ >> (pending_count_ - 8));
      pending_count_ -= 8;
      if (pending_count_ == 0)
        pending_ = 0;
      else
        pending_ &= (1u << pending_count_) - 1u;
      bytes_.push_back(byte);
      if (byte == 0xff)
        bytes_.push_back(0x00);
    }
  }

  std::vector<uint8_t> Finish() {
    if (pending_count_ != 0)
      Write(static_cast<uint16_t>((1u << (8 - pending_count_)) - 1u),
            static_cast<uint8_t>(8 - pending_count_));
    return bytes_;
  }

 private:
  std::vector<uint8_t> bytes_{};
  uint32_t pending_{0};
  uint8_t pending_count_{0};
};

uint8_t JpegCategory(int value) {
  uint32_t magnitude = static_cast<uint32_t>(value < 0 ? -value : value);
  uint8_t category = 0;
  while (magnitude != 0) {
    ++category;
    magnitude >>= 1;
  }
  return category;
}

uint16_t JpegMagnitudeBits(int value, uint8_t category) {
  if (category == 0)
    return 0;
  if (value >= 0)
    return static_cast<uint16_t>(value);
  return static_cast<uint16_t>(value + (1 << category) - 1);
}

// Fixture encoder: grayscale baseline JPEG with constant 8x8 MCUs. Edge MCUs
// are deliberately bright/dark so the decoder test proves centered cropping
// without using third-party artwork.
std::vector<uint8_t> MakeBaselineJpeg(uint16_t width = 128,
                                      uint16_t height = 64,
                                      bool alternating = false,
                                      uint8_t solid_sample = 0) {
  const std::array<uint8_t, 16> dc_counts{
      0, 1, 5, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0};
  const std::vector<uint8_t> dc_values{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11};
  const std::array<uint8_t, 16> ac_counts{
      1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
  const std::vector<uint8_t> ac_values{0x00};
  const auto dc_codes = MakeHuffmanCodes(dc_counts, dc_values);
  const auto ac_codes = MakeHuffmanCodes(ac_counts, ac_values);

  std::vector<uint8_t> jpeg{0xff, 0xd8};
  std::vector<uint8_t> dqt{0x00};
  dqt.insert(dqt.end(), 64, 1);
  AppendJpegSegment(&jpeg, 0xdb, dqt);

  std::vector<uint8_t> sof{8};
  AppendBe16(&sof, height);
  AppendBe16(&sof, width);
  sof.insert(sof.end(), {1, 1, 0x11, 0});
  AppendJpegSegment(&jpeg, 0xc0, sof);

  std::vector<uint8_t> dht{0x00};
  dht.insert(dht.end(), dc_counts.begin(), dc_counts.end());
  dht.insert(dht.end(), dc_values.begin(), dc_values.end());
  dht.push_back(0x10);
  dht.insert(dht.end(), ac_counts.begin(), ac_counts.end());
  dht.insert(dht.end(), ac_values.begin(), ac_values.end());
  AppendJpegSegment(&jpeg, 0xc4, dht);
  AppendJpegSegment(&jpeg, 0xda, {1, 1, 0x00, 0, 63, 0});

  EntropyWriter entropy;
  int previous_dc = 0;
  for (uint16_t block_y = 0; block_y < (height + 7) / 8; ++block_y) {
    (void) block_y;
    for (uint16_t block_x = 0; block_x < (width + 7) / 8; ++block_x) {
      const int sample = solid_sample != 0
                             ? solid_sample
                             : alternating ? (block_x & 1 ? 250 : 10)
                                           : block_x < 4 ? 10 : block_x < 8 ? 80
                                                                           : block_x < 12 ? 180 : 250;
      const int dc = (sample - 128) * 8;
      const int difference = dc - previous_dc;
      previous_dc = dc;
      const uint8_t category = JpegCategory(difference);
      entropy.Write(dc_codes[category].bits, dc_codes[category].size);
      entropy.Write(JpegMagnitudeBits(difference, category), category);
      entropy.Write(ac_codes[0].bits, ac_codes[0].size);
    }
  }
  const std::vector<uint8_t> entropy_bytes = entropy.Finish();
  jpeg.insert(jpeg.end(), entropy_bytes.begin(), entropy_bytes.end());
  jpeg.insert(jpeg.end(), {0xff, 0xd9});
  return jpeg;
}

size_t FindJpegMarker(const std::vector<uint8_t> &jpeg, uint8_t marker) {
  for (size_t i = 0; i + 1 < jpeg.size(); ++i)
    if (jpeg[i] == 0xff && jpeg[i + 1] == marker)
      return i;
  return jpeg.size();
}

uint8_t RedFrom565(uint16_t color) {
  return static_cast<uint8_t>(((color >> 11) & 0x1f) * 255 / 31);
}

uint8_t GreenFrom565(uint16_t color) {
  return static_cast<uint8_t>(((color >> 5) & 0x3f) * 255 / 63);
}

uint8_t BlueFrom565(uint16_t color) {
  return static_cast<uint8_t>((color & 0x1f) * 255 / 31);
}

std::vector<uint8_t> MakeSolidRgb(uint32_t width, uint32_t height,
                                  uint8_t red, uint8_t green, uint8_t blue) {
  std::vector<uint8_t> rgb(static_cast<size_t>(width) * height * 3);
  for (size_t index = 0; index < rgb.size(); index += 3) {
    rgb[index] = red;
    rgb[index + 1] = green;
    rgb[index + 2] = blue;
  }
  return rgb;
}

void AssertArtworkIsColor(
    const std::array<uint16_t, artwork::kArtworkPixelCount> &output,
    uint8_t red, uint8_t green, uint8_t blue) {
  const uint16_t expected = artwork::Rgb888ToRgb565(red, green, blue);
  for (uint16_t pixel : output)
    TEST_ASSERT_EQUAL_HEX16(expected, pixel);
}

void AssertArtworkNearGray(
    const std::array<uint16_t, artwork::kArtworkPixelCount> &output,
    uint8_t expected) {
  for (uint16_t pixel : output) {
    TEST_ASSERT_UINT8_WITHIN(8, expected, RedFrom565(pixel));
    TEST_ASSERT_UINT8_WITHIN(8, expected, GreenFrom565(pixel));
    TEST_ASSERT_UINT8_WITHIN(8, expected, BlueFrom565(pixel));
  }
}

struct CancellationProbe {
  size_t calls{0};
  size_t cancel_at{0};
};

bool CancelAtCall(void *opaque) {
  auto *probe = static_cast<CancellationProbe *>(opaque);
  ++probe->calls;
  return probe->calls >= probe->cancel_at;
}

// All JPEG inputs are original patterns encoded at runtime, not device captures.
enum class JpegFixtureFormat { kGray, k420, k422, k444, kRgb, kCmyk };

std::vector<uint8_t> MakeLibraryJpeg(uint32_t width, uint32_t height,
                                    JpegFixtureFormat format, bool progressive = true,
                                    unsigned restart_interval = 0,
                                    void *client_data = nullptr) {
  const unsigned channels = format == JpegFixtureFormat::kGray ? 1 :
                            format == JpegFixtureFormat::kCmyk ? 4 : 3;
  unsigned divisor = 1;
  while (divisor < 8 && std::min(width, height) >= 128 * divisor)
    divisor *= 2;
  std::vector<uint8_t> pixels(size_t{width} * height * channels);
  for (uint32_t y = 0; y < height; ++y) {
    for (uint32_t x = 0; x < width; ++x) {
      // Within-block contrast survives every tested reduced IDCT size.
      const uint8_t detail = ((x / divisor + y / divisor) % 4 < 2) ? 35 : 220;
      const size_t p = (size_t{y} * width + x) * channels;
      pixels[p] = detail;
      if (channels > 1) {
        pixels[p + 1] = static_cast<uint8_t>(40 + (y * 3) % 175);
        pixels[p + 2] = static_cast<uint8_t>(30 + (x * 5) % 195);
      }
      if (channels == 4) pixels[p + 3] = 0;
    }
  }
  jpeg_compress_struct jpeg{};
  jpeg_error_mgr error{};
  jpeg.err = jpeg_std_error(&error);
  jpeg.client_data = client_data;
  jpeg_create_compress(&jpeg);
  unsigned char *encoded = nullptr;
  unsigned long size = 0;
  jpeg_mem_dest(&jpeg, &encoded, &size);
  jpeg.image_width = width;
  jpeg.image_height = height;
  jpeg.input_components = channels;
  jpeg.in_color_space = channels == 1 ? JCS_GRAYSCALE :
                        channels == 4 ? JCS_CMYK : JCS_RGB;
  jpeg_set_defaults(&jpeg);
  if (format == JpegFixtureFormat::kRgb) jpeg_set_colorspace(&jpeg, JCS_RGB);
  if (channels == 3 && format != JpegFixtureFormat::kRgb) {
    jpeg.comp_info[0].h_samp_factor = format == JpegFixtureFormat::k444 ? 1 : 2;
    jpeg.comp_info[0].v_samp_factor = format == JpegFixtureFormat::k420 ? 2 : 1;
    for (unsigned c = 1; c < 3; ++c) {
      jpeg.comp_info[c].h_samp_factor = 1;
      jpeg.comp_info[c].v_samp_factor = 1;
    }
  }
  jpeg_set_quality(&jpeg, 92, TRUE);
  jpeg.restart_interval = restart_interval;
  if (progressive) jpeg_simple_progression(&jpeg);
  jpeg_start_compress(&jpeg, TRUE);
  while (jpeg.next_scanline < height) {
    JSAMPROW row = pixels.data() + size_t{jpeg.next_scanline} * width * channels;
    jpeg_write_scanlines(&jpeg, &row, 1);
  }
  jpeg_finish_compress(&jpeg);
  std::vector<uint8_t> result(encoded, encoded + size);
  jpeg_destroy_compress(&jpeg);
  TEST_ASSERT_TRUE(jpeg.client_data == client_data);
  std::free(encoded);
  return result;
}

// Independent final-image reference: ordinary libjpeg decoding, with neither
// buffered scans nor the bounded progressive wrapper's row callback.
std::vector<uint8_t> LibraryReference(const std::vector<uint8_t> &encoded,
                                      unsigned divisor, uint32_t *width,
                                      uint32_t *height,
                                      void *client_data = nullptr) {
  jpeg_decompress_struct jpeg{};
  jpeg_error_mgr error{};
  jpeg.err = jpeg_std_error(&error);
  jpeg.client_data = client_data;
  jpeg_create_decompress(&jpeg);
  jpeg_mem_src(&jpeg, encoded.data(), encoded.size());
  jpeg_read_header(&jpeg, TRUE);
  jpeg.scale_num = 1;
  jpeg.scale_denom = divisor;
  jpeg.out_color_space = JCS_RGB;
  jpeg_start_decompress(&jpeg);
  *width = jpeg.output_width;
  *height = jpeg.output_height;
  std::vector<uint8_t> result(size_t{*width} * *height * 3);
  while (jpeg.output_scanline < jpeg.output_height) {
    JSAMPROW row = result.data() + size_t{jpeg.output_scanline} * *width * 3;
    jpeg_read_scanlines(&jpeg, &row, 1);
  }
  jpeg_finish_decompress(&jpeg);
  jpeg_destroy_decompress(&jpeg);
  TEST_ASSERT_TRUE(jpeg.client_data == client_data);
  return result;
}

std::vector<size_t> JpegMarkerOffsets(const std::vector<uint8_t> &jpeg,
                                       uint8_t wanted) {
  std::vector<size_t> offsets;
  size_t p = 2;
  while (p + 1 < jpeg.size()) {
    if (jpeg[p] != 0xff) { ++p; continue; }
    const size_t start = p;
    while (p < jpeg.size() && jpeg[p] == 0xff) ++p;
    if (p == jpeg.size()) break;
    const uint8_t marker = jpeg[p++];
    if (marker == 0 || (marker >= 0xd0 && marker <= 0xd7)) continue;
    if (marker == wanted) offsets.push_back(start);
    if (marker == 0xd9) break;
    if (marker == 0x01) continue;
    if (p + 1 >= jpeg.size()) break;
    const size_t length = (size_t{jpeg[p]} << 8) | jpeg[p + 1];
    if (length < 2 || length > jpeg.size() - p) break;
    p += length;
  }
  return offsets;
}

struct ProgressiveProbe {
  std::vector<uint8_t> rgb;
  uint32_t width{0};
  uint32_t height{0};
  unsigned rows{0};
  unsigned calls{0};
  unsigned yields{0};
  unsigned cancel_at{0};
  unsigned cancel_after_rows{0};
  bool reject_row{false};
  bool delay_once{false};
};

int CollectProgressiveRow(void *opaque, uint32_t width, uint32_t height,
                           uint32_t y, const uint8_t *rgb) {
  auto *probe = static_cast<ProgressiveProbe *>(opaque);
  TEST_ASSERT_EQUAL_UINT(probe->rows, y);
  TEST_ASSERT_NOT_NULL(rgb);
  if (probe->rows == 0) { probe->width = width; probe->height = height; }
  TEST_ASSERT_EQUAL_UINT(probe->width, width);
  TEST_ASSERT_EQUAL_UINT(probe->height, height);
  probe->rgb.insert(probe->rgb.end(), rgb, rgb + width * 3);
  ++probe->rows;
  return !probe->reject_row;
}

int CancelProgressive(void *opaque) {
  auto *probe = static_cast<ProgressiveProbe *>(opaque);
  ++probe->calls;
  return (probe->cancel_at && probe->calls >= probe->cancel_at) ||
         (probe->cancel_after_rows && probe->rows >= probe->cancel_after_rows);
}

void YieldProgressive(void *opaque) {
  auto *probe = static_cast<ProgressiveProbe *>(opaque);
  ++probe->yields;
  if (probe->delay_once) {
    probe->delay_once = false;
    // No injectable clock exists; a yield callback advances real monotonic
    // time well beyond the deliberately tiny deadline.
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
}

pixoo_jpeg_options ProgressiveOptions(ProgressiveProbe *probe) {
  pixoo_jpeg_options options{};
  options.row = CollectProgressiveRow;
  options.cancel = CancelProgressive;
  options.yield = YieldProgressive;
  options.context = probe;
  return options;
}

pixoo_jpeg_status DecodeProgressiveFixture(const std::vector<uint8_t> &jpeg,
                                            uint32_t width, uint32_t height,
                                            const pixoo_jpeg_options &options,
                                            pixoo_jpeg_statistics *stats) {
  const auto status = pixoo_decode_progressive_jpeg(
      jpeg.data(), jpeg.size(), width, height, &options, stats);
  TEST_ASSERT_EQUAL_UINT(0, stats->live_memory);
  TEST_ASSERT_TRUE(stats->peak_memory <=
      (options.memory_limit ? std::min<size_t>(options.memory_limit, PIXOO_JPEG_MEMORY_LIMIT)
                            : PIXOO_JPEG_MEMORY_LIMIT));
  return status;
}

// Destination-driven integer area integration, independent of the production
// source-driven accumulation. Coordinates use units of 1/64 source pixel.
std::array<uint16_t, artwork::kArtworkPixelCount> ReferenceArtwork(
    const std::vector<uint8_t> &rgb, uint32_t width, uint32_t height) {
  std::array<uint16_t, artwork::kArtworkPixelCount> result{};
  const uint32_t extent = std::min(width, height);
  const uint32_t left = (width - extent) / 2;
  const uint32_t top = (height - extent) / 2;
  const uint32_t area = extent * extent;
  for (uint32_t dy = 0; dy < 64; ++dy) {
    for (uint32_t dx = 0; dx < 64; ++dx) {
      uint64_t sums[3]{};
      for (uint32_t sy = dy * extent / 64; sy < ((dy + 1) * extent + 63) / 64; ++sy) {
        const uint32_t wy = std::min((sy + 1) * 64, (dy + 1) * extent) -
                            std::max(sy * 64, dy * extent);
        for (uint32_t sx = dx * extent / 64; sx < ((dx + 1) * extent + 63) / 64; ++sx) {
          const uint32_t wx = std::min((sx + 1) * 64, (dx + 1) * extent) -
                              std::max(sx * 64, dx * extent);
          const size_t p = (size_t{sy + top} * width + sx + left) * 3;
          for (unsigned c = 0; c < 3; ++c) sums[c] += uint64_t{rgb[p + c]} * wx * wy;
        }
      }
      result[dy * 64 + dx] = artwork::Rgb888ToRgb565(
          (sums[0] + area / 2) / area, (sums[1] + area / 2) / area,
          (sums[2] + area / 2) / area);
    }
  }
  return result;
}

void AssertArtworkFailure(const std::vector<uint8_t> &jpeg,
                           artwork::DecodeStatus expected,
                           CancellationProbe *cancellation = nullptr) {
  std::array<uint16_t, artwork::kArtworkPixelCount> output;
  output.fill(0x5a5a);
  const auto status = artwork::DecodeArtwork(
      jpeg.data(), jpeg.size(), output.data(), output.size(), nullptr,
      cancellation ? CancelAtCall : nullptr, cancellation);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(expected), static_cast<int>(status));
  for (uint16_t pixel : output) TEST_ASSERT_EQUAL_HEX16(0x5a5a, pixel);
}

}  // namespace

static void test_entity_ids() {
  TEST_ASSERT_TRUE(cfg::ValidateEntityId("media_player.fixture_room", std::strlen("media_player.fixture_room")));
  TEST_ASSERT_TRUE(cfg::ValidateEntityId("media_player.fixture_room_2", std::strlen("media_player.fixture_room_2")));
  std::string longest = "media_player." + std::string(83, 'a');
  TEST_ASSERT_TRUE(cfg::ValidateEntityId(longest.data(), longest.size()));
  longest.push_back('a');
  TEST_ASSERT_FALSE(cfg::ValidateEntityId(longest.data(), longest.size()));
  for (const char *value : {"", "sensor.room", "media_player.", "media_player._room",
                            "media_player.room_", "media_player.room__two",
                            "media_player.Room", "media_player.room-name"})
    TEST_ASSERT_FALSE(cfg::ValidateEntityId(value, std::strlen(value)));
}

static void test_home_assistant_urls() {
  std::string output;
  TEST_ASSERT_TRUE(cfg::NormalizeHomeAssistantUrl("https://panel.invalid/proxy///", 30, &output));
  TEST_ASSERT_EQUAL_STRING("https://panel.invalid/proxy", output.c_str());
  TEST_ASSERT_TRUE(cfg::NormalizeHomeAssistantUrl("http://panel.invalid:8443/", 26, &output));
  TEST_ASSERT_EQUAL_STRING("http://panel.invalid:8443", output.c_str());
  for (const char *value : {"//panel.invalid", "https://user@panel.invalid", "https://panel.invalid?x=y",
                            "https://panel.invalid/#x", "https://panel.invalid/a b",
                            "https://panel.invalid:0", "https://panel.invalid:65536",
                            "https://[bad]", "https://[2001:::1]", "https://2001:db8::1"})
    TEST_ASSERT_FALSE(cfg::NormalizeHomeAssistantUrl(value, std::strlen(value), &output));
  std::string limit = "https://panel.invalid/" + std::string(234, 'a');
  TEST_ASSERT_EQUAL_UINT(256, limit.size());
  TEST_ASSERT_TRUE(cfg::NormalizeHomeAssistantUrl(limit.data(), limit.size(), &output));
  limit.push_back('a');
  TEST_ASSERT_FALSE(cfg::NormalizeHomeAssistantUrl(limit.data(), limit.size(), &output));
}

static void test_artwork_urls() {
  std::string output;
  const char *base = "https://panel.invalid/prefix";
  const char *relative = "/api/art?sig=opaque&cache=1";
  TEST_ASSERT_TRUE(cfg::ResolveArtworkUrl(base, std::strlen(base), relative, std::strlen(relative), &output));
  TEST_ASSERT_EQUAL_STRING("https://panel.invalid/prefix/api/art?sig=opaque&cache=1", output.c_str());
  std::string equivalent_absolute;
  const char *same_absolute =
      "https://panel.invalid/prefix/api/art?sig=opaque&cache=1";
  TEST_ASSERT_TRUE(cfg::ResolveArtworkUrl(
      base, std::strlen(base), same_absolute, std::strlen(same_absolute),
      &equivalent_absolute));
  TEST_ASSERT_EQUAL_STRING(output.c_str(), equivalent_absolute.c_str());
  const char *absolute = "http://art.invalid/path?sig=opaque";
  TEST_ASSERT_TRUE(cfg::ResolveArtworkUrl(base, std::strlen(base), absolute, std::strlen(absolute), &output));
  TEST_ASSERT_EQUAL_STRING(absolute, output.c_str());
  for (const char *value : {"//art.invalid/a", "https://user@art.invalid/a", "https://art.invalid/a#x",
                            "ftp://art.invalid/a", "relative/a"})
    TEST_ASSERT_FALSE(cfg::ResolveArtworkUrl(base, std::strlen(base), value, std::strlen(value), &output));
  std::string oversized(769, 'a');
  oversized[0] = '/';
  TEST_ASSERT_FALSE(cfg::ResolveArtworkUrl(base, std::strlen(base), oversized.data(), oversized.size(), &output));
}

static void test_artwork_fetch_policy_and_magic() {
  namespace artwork = esphome::pixoo64::artwork;
  using artwork::ImageMagic;
  const uint8_t png[] = {0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a};
  const uint8_t jpeg[] = {0xff, 0xd8, 0xff};
  const uint8_t gif[] = {'G', 'I', 'F', '8', '9', 'a'};
  const uint8_t webp[] = {'R', 'I', 'F', 'F', 0, 0, 0, 0, 'W', 'E', 'B', 'P'};
  TEST_ASSERT_EQUAL_INT(static_cast<int>(ImageMagic::kPng), static_cast<int>(artwork::ClassifyMagic(png, sizeof(png))));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(ImageMagic::kJpeg), static_cast<int>(artwork::ClassifyMagic(jpeg, sizeof(jpeg))));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(ImageMagic::kGif), static_cast<int>(artwork::ClassifyMagic(gif, sizeof(gif))));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(ImageMagic::kWebp), static_cast<int>(artwork::ClassifyMagic(webp, sizeof(webp))));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(ImageMagic::kUnknown), static_cast<int>(artwork::ClassifyMagic(nullptr, 0)));
  namespace body = esphome::pixoo64::http_body;
  namespace weather = esphome::pixoo64::adapters::weather;
  TEST_ASSERT_TRUE(artwork::AcceptBodySize(artwork::kMaxEncodedBytes, false));
  TEST_ASSERT_FALSE(artwork::AcceptBodySize(artwork::kMaxEncodedBytes + 1, false));
  TEST_ASSERT_TRUE(artwork::AcceptBodySize(artwork::kMaxEncodedBytes + 1, true));
  TEST_ASSERT_TRUE(artwork::IsCompleteBody(4, 4, false, true));
  TEST_ASSERT_FALSE(artwork::IsCompleteBody(3, 4, false, true));
  TEST_ASSERT_FALSE(artwork::IsCompleteBody(4, 4, true, false));
  TEST_ASSERT_FALSE(artwork::IsCompleteBody(artwork::kMaxEncodedBytes + 1, 0, true, true));
  // Fixed-length transfers are exact; chunked/unknown transfers rely on the
  // transport completion signal while still respecting the caller's cap.
  TEST_ASSERT_TRUE(body::IsCompleteBody(4, 4, true, 16, true));
  TEST_ASSERT_FALSE(body::IsCompleteBody(3, 4, true, 16, true));
  TEST_ASSERT_TRUE(body::IsCompleteBody(4, 0, false, 16, true));
  TEST_ASSERT_FALSE(body::IsCompleteBody(4, 0, false, 16, false));
  TEST_ASSERT_TRUE(body::AcceptAdvertisedSize(weather::kMaxResponseBytes, true,
                                              weather::kMaxResponseBytes));
  TEST_ASSERT_FALSE(body::AcceptAdvertisedSize(weather::kMaxResponseBytes + 1,
                                               true, weather::kMaxResponseBytes));
  const uint8_t body_a[] = {0x89, 'P', 'N', 'G'};
  const uint8_t body_b[] = {0x89, 'P', 'N', 'G'};
  const uint8_t body_c[] = {0x89, 'P', 'N', 'X'};
  TEST_ASSERT_TRUE(artwork::EncodedBodiesEqual(body_a, sizeof(body_a), body_b,
                                                sizeof(body_b)));
  TEST_ASSERT_FALSE(artwork::EncodedBodiesEqual(body_a, sizeof(body_a), body_c,
                                                 sizeof(body_c)));
  TEST_ASSERT_FALSE(artwork::EncodedBodiesEqual(body_a, sizeof(body_a), body_b,
                                                 sizeof(body_b) - 1));
  TEST_ASSERT_FALSE(artwork::EncodedBodiesEqual(nullptr, 0, nullptr, 0));

  artwork::FetchPolicy policy;
  policy.SetDesired(17);
  policy.SetVisible(true);
  const uint32_t generation = policy.generation();
  TEST_ASSERT_TRUE(policy.ShouldStart(0xfffffff0u, false, false));
  TEST_ASSERT_TRUE(policy.Accepts(generation, 17));
  TEST_ASSERT_TRUE(policy.Failed(0xfffffff0u));
  TEST_ASSERT_FALSE(policy.ShouldStart(0x00000010u, false, false));
  TEST_ASSERT_TRUE(policy.ShouldStart(0x00001380u, false, false));
  policy.SetVisible(false);
  TEST_ASSERT_FALSE(policy.Accepts(generation, 17));
  policy.SetVisible(true);
  TEST_ASSERT_TRUE(policy.ShouldStart(0x00000010u, false, false));
  policy.SetDesired(18);
  TEST_ASSERT_TRUE(policy.ShouldStart(0x00000010u, false, false));
  const uint32_t changed_generation = policy.generation();
  policy.SetDesired(18, true);
  TEST_ASSERT_NOT_EQUAL(changed_generation, policy.generation());
  policy.Succeeded();
  TEST_ASSERT_FALSE(policy.ShouldStart(0x00000010u, true, false));

  artwork::FetchPolicy retries;
  retries.SetDesired(99);
  retries.SetVisible(true);
  TEST_ASSERT_TRUE(retries.Failed(0));
  TEST_ASSERT_TRUE(retries.ShouldStart(5000, false, false));
  TEST_ASSERT_TRUE(retries.Failed(5000));
  TEST_ASSERT_TRUE(retries.ShouldStart(20000, false, false));
  TEST_ASSERT_TRUE(retries.Failed(20000));
  TEST_ASSERT_TRUE(retries.ShouldStart(80000, false, false));
  TEST_ASSERT_FALSE(retries.Failed(80000));
  TEST_ASSERT_FALSE(retries.ShouldStart(80001, false, false));
  retries.SetVisible(false);
  retries.SetVisible(true);
  TEST_ASSERT_TRUE(retries.ShouldStart(80001, false, false));

  artwork::FetchPolicy reconnect_retries;
  reconnect_retries.SetDesired(101);
  reconnect_retries.SetVisible(true);
  TEST_ASSERT_TRUE(reconnect_retries.Failed(0));
  TEST_ASSERT_TRUE(reconnect_retries.Failed(5000));
  TEST_ASSERT_TRUE(reconnect_retries.Failed(20000));
  TEST_ASSERT_FALSE(reconnect_retries.Failed(80000));
  TEST_ASSERT_FALSE(reconnect_retries.ShouldStart(80001, false, false));
  reconnect_retries.ResetRetry();
  TEST_ASSERT_TRUE(reconnect_retries.ShouldStart(80001, false, false));
  TEST_ASSERT_FALSE(reconnect_retries.ShouldStart(80001, true, false));

  uint32_t pins[artwork::kArtworkSlotCount]{0, 0};
  TEST_ASSERT_EQUAL_INT8(0, artwork::SelectWritableSlot(-1, pins));
  TEST_ASSERT_EQUAL_INT8(1, artwork::SelectWritableSlot(0, pins));
  pins[1] = 1;
  TEST_ASSERT_EQUAL_INT8(-1, artwork::SelectWritableSlot(0, pins));
  pins[0] = 1;
  pins[1] = 0;
  TEST_ASSERT_EQUAL_INT8(1, artwork::SelectWritableSlot(-1, pins));
}

static void test_artwork_crop_mapping_and_color_math() {
  TEST_ASSERT_EQUAL_UINT32(512 * 1024, artwork::kMaxEncodedBytes);
  TEST_ASSERT_EQUAL_UINT32(4096, artwork::kMaxSourceWidth);
  TEST_ASSERT_EQUAL_UINT32(4096, artwork::kMaxSourceHeight);
  TEST_ASSERT_EQUAL_UINT32(16'777'216, artwork::kMaxSourcePixels);
  TEST_ASSERT_EQUAL_UINT32(16'777'216, artwork::kMaxDecodeCallbacks);
  TEST_ASSERT_EQUAL_UINT32(4096, artwork::kArtworkPixelCount);
  const artwork::CropRect wide = artwork::CenterCrop(100, 64);
  TEST_ASSERT_EQUAL_UINT32(18, wide.x);
  TEST_ASSERT_EQUAL_UINT32(0, wide.y);
  TEST_ASSERT_EQUAL_UINT32(64, wide.width);
  TEST_ASSERT_EQUAL_UINT32(64, wide.height);
  const artwork::CropRect tall = artwork::CenterCrop(65, 100);
  TEST_ASSERT_EQUAL_UINT32(0, tall.x);
  TEST_ASSERT_EQUAL_UINT32(17, tall.y);
  TEST_ASSERT_EQUAL_UINT32(65, tall.width);
  TEST_ASSERT_EQUAL_UINT32(65, tall.height);

  TEST_ASSERT_EQUAL_HEX16(0xf800, artwork::Rgb888ToRgb565(255, 0, 0));
  TEST_ASSERT_EQUAL_HEX16(0x07e0, artwork::Rgb888ToRgb565(0, 255, 0));
  TEST_ASSERT_EQUAL_HEX16(0x001f, artwork::Rgb888ToRgb565(0, 0, 255));
}

static void test_png_chunk_validation() {
  std::vector<uint8_t> rgb_pixels(2 * 2 * 3, 0xff);
  const std::vector<uint8_t> png = MakeRgbPng(2, 2, rgb_pixels);
  artwork::ImageInfo info{};
  TEST_ASSERT_EQUAL_INT(static_cast<int>(artwork::DecodeStatus::kSuccess),
      static_cast<int>(artwork::InspectPng(png.data(), png.size(), &info)));
  TEST_ASSERT_EQUAL_UINT32(2, info.width);
  TEST_ASSERT_EQUAL_UINT32(2, info.height);
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(artwork::DecodeStatus::kEncodedTooLarge),
      static_cast<int>(artwork::InspectPng(
          png.data(), artwork::kMaxEncodedBytes + 1, &info)));

  std::vector<uint8_t> rgba_pixels(2 * 2 * 4, 0xff);
  const std::vector<uint8_t> alpha_png = MakeRgbaPng(2, 2, rgba_pixels);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(artwork::DecodeStatus::kUnsupportedFormat),
      static_cast<int>(artwork::InspectPng(alpha_png.data(), alpha_png.size(), &info)));
  const std::vector<uint8_t> trns_png = MakeRgbPng(2, 2, rgb_pixels, true);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(artwork::DecodeStatus::kUnsupportedFormat),
      static_cast<int>(artwork::InspectPng(trns_png.data(), trns_png.size(), &info)));
  const std::vector<uint8_t> animated_png =
      MakeRgbPng(2, 2, rgb_pixels, false, false, true);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(artwork::DecodeStatus::kAnimatedPng),
      static_cast<int>(artwork::InspectPng(
          animated_png.data(), animated_png.size(), &info)));
  const std::vector<uint8_t> interlaced_png =
      MakeRgbPng(2, 2, rgb_pixels, false, true);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(artwork::DecodeStatus::kInterlacedPng),
      static_cast<int>(artwork::InspectPng(
          interlaced_png.data(), interlaced_png.size(), &info)));

  std::vector<uint8_t> malformed_length = png;
  // IHDR occupies bytes 8..32; make the following IDAT length exceed the body.
  malformed_length[33] = 0x7f;
  TEST_ASSERT_EQUAL_INT(static_cast<int>(artwork::DecodeStatus::kIncomplete),
      static_cast<int>(artwork::InspectPng(malformed_length.data(),
                                           malformed_length.size(), &info)));
  std::vector<uint8_t> bad_crc = png;
  bad_crc[20] ^= 1;
  TEST_ASSERT_EQUAL_INT(static_cast<int>(artwork::DecodeStatus::kMalformed),
      static_cast<int>(artwork::InspectPng(bad_crc.data(), bad_crc.size(), &info)));
  std::vector<uint8_t> truncated = png;
  truncated.pop_back();
  TEST_ASSERT_EQUAL_INT(static_cast<int>(artwork::DecodeStatus::kIncomplete),
      static_cast<int>(artwork::InspectPng(truncated.data(), truncated.size(), &info)));

  std::vector<uint8_t> oversized_pixels(4097 * 3, 0xff);
  const std::vector<uint8_t> oversized =
      MakeRgbPng(4097, 1, oversized_pixels);
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(artwork::DecodeStatus::kDimensionsTooLarge),
      static_cast<int>(artwork::InspectPng(oversized.data(), oversized.size(), &info)));
}

static void test_jpeg_marker_validation() {
  const std::vector<uint8_t> jpeg = MakeBaselineJpeg();
  TEST_ASSERT_GREATER_THAN_UINT(256, jpeg.size());
  artwork::ImageInfo info{};
  TEST_ASSERT_EQUAL_INT(static_cast<int>(artwork::DecodeStatus::kSuccess),
      static_cast<int>(artwork::InspectJpeg(jpeg.data(), jpeg.size(), &info)));
  TEST_ASSERT_EQUAL_UINT32(128, info.width);
  TEST_ASSERT_EQUAL_UINT32(64, info.height);

  const size_t sof = FindJpegMarker(jpeg, 0xc0);
  TEST_ASSERT_LESS_THAN_UINT(jpeg.size(), sof);
  // A sequential scan (Ss=0, Se=63) remains invalid when only SOF is changed;
  // real progressive DC scans require Ss=Se=0.
  std::vector<uint8_t> progressive = jpeg;
  progressive[sof + 1] = 0xc2;
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(artwork::DecodeStatus::kMalformed),
      static_cast<int>(artwork::InspectJpeg(progressive.data(),
                                            progressive.size(), &info)));

  std::vector<uint8_t> oversized = jpeg;
  oversized[sof + 7] = 0x10;
  oversized[sof + 8] = 0x01;
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(artwork::DecodeStatus::kDimensionsTooLarge),
      static_cast<int>(artwork::InspectJpeg(oversized.data(), oversized.size(), &info)));

  std::vector<uint8_t> bad_length = jpeg;
  const size_t dqt = FindJpegMarker(jpeg, 0xdb);
  bad_length[dqt + 2] = 0;
  bad_length[dqt + 3] = 1;
  TEST_ASSERT_EQUAL_INT(static_cast<int>(artwork::DecodeStatus::kIncomplete),
      static_cast<int>(artwork::InspectJpeg(bad_length.data(),
                                            bad_length.size(), &info)));

  std::vector<uint8_t> truncated = jpeg;
  truncated.resize(truncated.size() - 2);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(artwork::DecodeStatus::kIncomplete),
      static_cast<int>(artwork::InspectJpeg(truncated.data(), truncated.size(), &info)));
}

static void test_actual_png_decode_area_resampling() {
  std::array<uint16_t, artwork::kArtworkPixelCount> output{};
  output.fill(0x5a5a);

  std::vector<uint8_t> alpha_pixels(2 * 2 * 4, 0xff);
  const std::vector<uint8_t> alpha_png = MakeRgbaPng(2, 2, alpha_pixels);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(artwork::DecodeStatus::kUnsupportedFormat),
      static_cast<int>(artwork::DecodeArtwork(
          alpha_png.data(), alpha_png.size(), output.data(), output.size())));
  for (uint16_t pixel : output)
    TEST_ASSERT_EQUAL_HEX16(0x5a5a, pixel);

  std::vector<uint8_t> trns_pixels(2 * 2 * 3, 0xff);
  const std::vector<uint8_t> trns_png = MakeRgbPng(2, 2, trns_pixels, true);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(artwork::DecodeStatus::kUnsupportedFormat),
      static_cast<int>(artwork::DecodeArtwork(
          trns_png.data(), trns_png.size(), output.data(), output.size())));

  constexpr uint8_t red = 123;
  constexpr uint8_t green = 45;
  constexpr uint8_t blue = 200;
  for (uint32_t extent : {1u, 2u, 63u, 64u, 65u, 96u, 255u}) {
    const std::vector<uint8_t> png = MakeRgbPng(
        extent, extent, MakeSolidRgb(extent, extent, red, green, blue));
    TEST_ASSERT_LESS_THAN_UINT(artwork::kMaxEncodedBytes, png.size());
    TEST_ASSERT_EQUAL_INT(static_cast<int>(artwork::DecodeStatus::kSuccess),
        static_cast<int>(artwork::DecodeArtwork(
            png.data(), png.size(), output.data(), output.size())));
    AssertArtworkIsColor(output, red, green, blue);
  }
  for (const std::array<uint32_t, 2> dimensions : {
           std::array<uint32_t, 2>{97, 63},
           std::array<uint32_t, 2>{63, 97},
           std::array<uint32_t, 2>{255, 65},
       }) {
    const std::vector<uint8_t> png = MakeRgbPng(
        dimensions[0], dimensions[1],
        MakeSolidRgb(dimensions[0], dimensions[1], red, green, blue));
    TEST_ASSERT_LESS_THAN_UINT(artwork::kMaxEncodedBytes, png.size());
    TEST_ASSERT_EQUAL_INT(static_cast<int>(artwork::DecodeStatus::kSuccess),
        static_cast<int>(artwork::DecodeArtwork(
            png.data(), png.size(), output.data(), output.size())));
    AssertArtworkIsColor(output, red, green, blue);
  }

  constexpr uint32_t width = 128;
  constexpr uint32_t height = 96;
  std::vector<uint8_t> crop_pixels(width * height * 3, 0);
  for (uint32_t y = 0; y < height; ++y) {
    for (uint32_t x = 0; x < width; ++x) {
      const size_t index = (static_cast<size_t>(y) * width + x) * 3;
      crop_pixels[index + 1] = 255;
      if (x == 16) {
        crop_pixels[index] = 255;
        crop_pixels[index + 1] = 0;
      }
    }
  }
  const std::vector<uint8_t> crop_png =
      MakeRgbPng(width, height, crop_pixels);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(artwork::DecodeStatus::kSuccess),
      static_cast<int>(artwork::DecodeArtwork(
          crop_png.data(), crop_png.size(), output.data(), output.size())));
  // The first destination pixel covers source columns 16 and 17 in a 2:1
  // ratio.
  TEST_ASSERT_EQUAL_HEX16(artwork::Rgb888ToRgb565(170, 85, 0), output[0]);
  for (uint32_t x = 1; x < 64; ++x)
    TEST_ASSERT_EQUAL_HEX16(0x07e0, output[x]);

  const std::vector<uint8_t> small_png = MakeRgbPng(1, 1, {123, 45, 200});
  TEST_ASSERT_EQUAL_INT(static_cast<int>(artwork::DecodeStatus::kSuccess),
      static_cast<int>(artwork::DecodeArtwork(
          small_png.data(), small_png.size(), output.data(), output.size())));
  for (uint16_t pixel : output)
    TEST_ASSERT_EQUAL_HEX16(artwork::Rgb888ToRgb565(123, 45, 200), pixel);

  std::vector<uint8_t> rounding_pixels(96 * 96 * 3, 0);
  for (uint32_t y = 0; y < 96; ++y)
    rounding_pixels[(static_cast<size_t>(y) * 96 + 1) * 3] = 23;
  const std::vector<uint8_t> rounding_png =
      MakeRgbPng(96, 96, rounding_pixels);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(artwork::DecodeStatus::kSuccess),
      static_cast<int>(artwork::DecodeArtwork(
          rounding_png.data(), rounding_png.size(), output.data(), output.size())));
  TEST_ASSERT_EQUAL_HEX16(artwork::Rgb888ToRgb565(8, 0, 0), output[0]);

  std::vector<uint8_t> incomplete = crop_png;
  incomplete.pop_back();
  output.fill(0x5a5a);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(artwork::DecodeStatus::kIncomplete),
      static_cast<int>(artwork::DecodeArtwork(
          incomplete.data(), incomplete.size(), output.data(), output.size())));
  for (uint16_t pixel : output)
    TEST_ASSERT_EQUAL_HEX16(0x5a5a, pixel);
}

static void test_png_decode_cancellation() {
  constexpr uint32_t width = 64;
  constexpr uint32_t height = 64;
  std::vector<uint8_t> pixels(width * height * 3, 0);
  for (size_t i = 0; i < pixels.size(); i += 3)
    pixels[i] = 255;
  const std::vector<uint8_t> png = MakeRgbPng(width, height, pixels);
  std::array<uint16_t, artwork::kArtworkPixelCount> output{};
  output.fill(0x5a5a);

  artwork::ImageInfo info{};
  CancellationProbe immediate{0, 1};
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(artwork::DecodeStatus::kCancelled),
      static_cast<int>(artwork::DecodeArtwork(
          png.data(), png.size(), output.data(), output.size(), &info,
          CancelAtCall, &immediate)));
  TEST_ASSERT_EQUAL_UINT(1, immediate.calls);
  TEST_ASSERT_EQUAL_UINT32(width, info.width);
  TEST_ASSERT_EQUAL_UINT32(height, info.height);
  for (uint16_t pixel : output)
    TEST_ASSERT_EQUAL_HEX16(0x5a5a, pixel);

  std::vector<uint8_t> incomplete = png;
  incomplete.pop_back();
  CancellationProbe validation_first{0, 1};
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(artwork::DecodeStatus::kIncomplete),
      static_cast<int>(artwork::DecodeArtwork(
          incomplete.data(), incomplete.size(), output.data(), output.size(),
          nullptr, CancelAtCall, &validation_first)));
  TEST_ASSERT_EQUAL_UINT(0, validation_first.calls);

  output.fill(0x5a5a);
  CancellationProbe after_draws{0, 75};
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(artwork::DecodeStatus::kCancelled),
      static_cast<int>(artwork::DecodeArtwork(
          png.data(), png.size(), output.data(), output.size(), nullptr,
          CancelAtCall, &after_draws)));
  TEST_ASSERT_EQUAL_UINT(75, after_draws.calls);
  for (uint16_t pixel : output)
    TEST_ASSERT_EQUAL_HEX16(0x5a5a, pixel);
}

static void test_actual_baseline_jpeg_decode_and_rejections() {
  const std::vector<uint8_t> jpeg = MakeBaselineJpeg();
  std::array<uint16_t, artwork::kArtworkPixelCount> output{};
  artwork::ImageInfo info{};
  TEST_ASSERT_EQUAL_INT(static_cast<int>(artwork::DecodeStatus::kSuccess),
      static_cast<int>(artwork::DecodeArtwork(
          jpeg.data(), jpeg.size(), output.data(), output.size(),
          &info)));
  TEST_ASSERT_EQUAL_UINT32(128, info.width);
  TEST_ASSERT_EQUAL_UINT32(64, info.height);
  TEST_ASSERT_UINT8_WITHIN(8, 80, RedFrom565(output[0]));
  TEST_ASSERT_UINT8_WITHIN(8, 80, GreenFrom565(output[0]));
  TEST_ASSERT_UINT8_WITHIN(8, 80, BlueFrom565(output[0]));
  TEST_ASSERT_UINT8_WITHIN(8, 180, RedFrom565(output[63]));
  TEST_ASSERT_UINT8_WITHIN(8, 180, GreenFrom565(output[63]));
  TEST_ASSERT_UINT8_WITHIN(8, 180, BlueFrom565(output[63]));

  const std::vector<uint8_t> area_jpeg = MakeBaselineJpeg(96, 96, true);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(artwork::DecodeStatus::kSuccess),
      static_cast<int>(artwork::DecodeArtwork(
          area_jpeg.data(), area_jpeg.size(), output.data(), output.size())));
  // Destination column 5 spans half of a dark source pixel and one bright
  // source pixel. Its area average is near 170, not the bright nearest sample.
  TEST_ASSERT_UINT8_WITHIN(24, 170, RedFrom565(output[5]));
  TEST_ASSERT_UINT8_WITHIN(24, 170, GreenFrom565(output[5]));
  TEST_ASSERT_UINT8_WITHIN(24, 170, BlueFrom565(output[5]));

  output.fill(0x5a5a);
  std::vector<uint8_t> progressive = jpeg;
  const size_t sof = FindJpegMarker(progressive, 0xc0);
  progressive[sof + 1] = 0xc2;
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(artwork::DecodeStatus::kMalformed),
      static_cast<int>(artwork::DecodeArtwork(
          progressive.data(), progressive.size(), output.data(), output.size())));
  for (uint16_t pixel : output)
    TEST_ASSERT_EQUAL_HEX16(0x5a5a, pixel);
  std::vector<uint8_t> incomplete = jpeg;
  incomplete.pop_back();
  TEST_ASSERT_EQUAL_INT(static_cast<int>(artwork::DecodeStatus::kIncomplete),
      static_cast<int>(artwork::DecodeArtwork(
          incomplete.data(), incomplete.size(), output.data(), output.size())));
}

static void test_jpeg_reduced_decode_callback_tiling() {
  struct Fixture {
    uint16_t width;
    uint16_t height;
  };
  // These dimensions select half, quarter, and eighth reduced IDCT output.
  // Their non-MCU edges require JPEGDEC to clip callback rectangles while
  // still tiling ceil(source/divisor) pixels in row-band order.
  for (const Fixture fixture : {
           Fixture{129, 130}, Fixture{257, 258}, Fixture{513, 514},
       }) {
    const std::vector<uint8_t> jpeg =
        MakeBaselineJpeg(fixture.width, fixture.height, false, 120);
    TEST_ASSERT_LESS_THAN_UINT(artwork::kMaxEncodedBytes, jpeg.size());
    std::array<uint16_t, artwork::kArtworkPixelCount> output{};
    artwork::ImageInfo info{};
    TEST_ASSERT_EQUAL_INT(static_cast<int>(artwork::DecodeStatus::kSuccess),
        static_cast<int>(artwork::DecodeArtwork(
            jpeg.data(), jpeg.size(), output.data(), output.size(), &info)));
    TEST_ASSERT_EQUAL_UINT32(fixture.width, info.width);
    TEST_ASSERT_EQUAL_UINT32(fixture.height, info.height);
    AssertArtworkNearGray(output, 120);
  }
}

static void test_jpeg_decode_cancellation() {
  const std::vector<uint8_t> jpeg = MakeBaselineJpeg();
  std::array<uint16_t, artwork::kArtworkPixelCount> output{};
  output.fill(0x5a5a);

  artwork::ImageInfo info{};
  CancellationProbe immediate{0, 1};
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(artwork::DecodeStatus::kCancelled),
      static_cast<int>(artwork::DecodeArtwork(
          jpeg.data(), jpeg.size(), output.data(), output.size(),
          &info, CancelAtCall, &immediate)));
  TEST_ASSERT_EQUAL_UINT(1, immediate.calls);
  TEST_ASSERT_EQUAL_UINT32(128, info.width);
  TEST_ASSERT_EQUAL_UINT32(64, info.height);
  for (uint16_t pixel : output)
    TEST_ASSERT_EQUAL_HEX16(0x5a5a, pixel);

  output.fill(0x5a5a);
  CancellationProbe after_draws{0, 13};
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(artwork::DecodeStatus::kCancelled),
      static_cast<int>(artwork::DecodeArtwork(
          jpeg.data(), jpeg.size(), output.data(), output.size(),
          nullptr, CancelAtCall, &after_draws)));
  TEST_ASSERT_EQUAL_UINT(13, after_draws.calls);
  for (uint16_t pixel : output)
    TEST_ASSERT_EQUAL_HEX16(0x5a5a, pixel);
}

static void test_progressive_final_image_formats_scaling_and_detail() {
  for (const auto format : {JpegFixtureFormat::kGray, JpegFixtureFormat::k420,
                            JpegFixtureFormat::k422, JpegFixtureFormat::k444}) {
    for (const uint32_t shorter : {63u, 129u, 257u, 513u}) {
      const uint32_t width = shorter + 10;
      const uint32_t height = shorter;
      unsigned divisor = 1;
      while (divisor < 8 && shorter >= 128 * divisor) divisor *= 2;
      const auto jpeg = MakeLibraryJpeg(width, height, format);
      artwork::ImageInfo info{};
      TEST_ASSERT_EQUAL_INT(static_cast<int>(artwork::DecodeStatus::kSuccess),
          static_cast<int>(artwork::InspectArtwork(jpeg.data(), jpeg.size(), &info)));
      TEST_ASSERT_EQUAL_INT(static_cast<int>(artwork::JpegMode::kProgressive),
                            static_cast<int>(info.jpeg_mode));
      TEST_ASSERT_EQUAL_UINT(width, info.width);
      TEST_ASSERT_EQUAL_UINT(height, info.height);
      const auto scans = JpegMarkerOffsets(jpeg, 0xda);
      TEST_ASSERT_GREATER_THAN_UINT(1, scans.size());
      uint32_t ref_width = 0, ref_height = 0;
      const auto reference = LibraryReference(jpeg, divisor, &ref_width, &ref_height);
      // Baseline and progressive encoders produce the same coefficients. Use
      // libjpeg for both references, not JPEGDEC's different IDCT/upsampling.
      const auto baseline = MakeLibraryJpeg(width, height, format, false);
      uint32_t baseline_width = 0, baseline_height = 0;
      const auto baseline_reference = LibraryReference(
          baseline, divisor, &baseline_width, &baseline_height);
      TEST_ASSERT_EQUAL_UINT(ref_width, baseline_width);
      TEST_ASSERT_EQUAL_UINT(ref_height, baseline_height);
      TEST_ASSERT_EQUAL_UINT8_ARRAY(reference.data(), baseline_reference.data(), reference.size());
      ProgressiveProbe probe;
      auto options = ProgressiveOptions(&probe);
      pixoo_jpeg_statistics stats{};
      TEST_ASSERT_EQUAL_INT(PIXOO_JPEG_SUCCESS,
          DecodeProgressiveFixture(jpeg, width, height, options, &stats));
      TEST_ASSERT_EQUAL_UINT((width + divisor - 1) / divisor, stats.width);
      TEST_ASSERT_EQUAL_UINT((height + divisor - 1) / divisor, stats.height);
      TEST_ASSERT_EQUAL_UINT(ref_height, probe.rows);
      TEST_ASSERT_EQUAL_UINT(scans.size(), stats.scans);
      TEST_ASSERT_GREATER_THAN_UINT(0, stats.peak_memory);
      TEST_ASSERT_GREATER_THAN_UINT(probe.rows, probe.yields);
      TEST_ASSERT_EQUAL_UINT(reference.size(), probe.rgb.size());
      TEST_ASSERT_EQUAL_UINT8_ARRAY(reference.data(), probe.rgb.data(), reference.size());
      // A DC-only decoder makes each 8x8 block constant. This fixture has
      // substantial contrast *inside* a block, even at reduced IDCT sizes.
      if (divisor == 1) {
        int contrast = int(probe.rgb[0]) - int(probe.rgb[2 * 3]);
        TEST_ASSERT_TRUE(std::abs(contrast) > 80);
      }
      std::array<uint16_t, artwork::kArtworkPixelCount> output{};
      TEST_ASSERT_EQUAL_INT(static_cast<int>(artwork::DecodeStatus::kSuccess),
          static_cast<int>(artwork::DecodeArtwork(
              jpeg.data(), jpeg.size(), output.data(), output.size(), &info)));
      const auto expected = ReferenceArtwork(reference, ref_width, ref_height);
      TEST_ASSERT_EQUAL_HEX16_ARRAY(expected.data(), output.data(), output.size());
    }
  }
  // Portrait and sub-pixel upscaling use the same centered crop integration.
  for (const auto dimensions : {std::array<uint32_t, 2>{1, 1},
                                std::array<uint32_t, 2>{17, 29},
                                std::array<uint32_t, 2>{65, 99}}) {
    const auto jpeg = MakeLibraryJpeg(dimensions[0], dimensions[1], JpegFixtureFormat::k444);
    uint32_t w = 0, h = 0;
    const auto reference = LibraryReference(jpeg, 1, &w, &h);
    const auto expected = ReferenceArtwork(reference, w, h);
    std::array<uint16_t, artwork::kArtworkPixelCount> output{};
    TEST_ASSERT_EQUAL_INT(static_cast<int>(artwork::DecodeStatus::kSuccess),
        static_cast<int>(artwork::DecodeArtwork(jpeg.data(), jpeg.size(), output.data(), output.size())));
    TEST_ASSERT_EQUAL_HEX16_ARRAY(expected.data(), output.data(), output.size());
  }
}

static void test_libjpeg_foreign_client_data() {
  struct ForeignClientData {
    uint32_t sentinel;
  } foreign{0x13579bdfu};
  const auto jpeg = MakeLibraryJpeg(73, 65, JpegFixtureFormat::k420,
                                    true, 0, &foreign);
  TEST_ASSERT_EQUAL_HEX32(0x13579bdfu, foreign.sentinel);
  TEST_ASSERT_GREATER_THAN_UINT(1, JpegMarkerOffsets(jpeg, 0xda).size());
  uint32_t width = 0, height = 0;
  // Progressive coefficient arrays are virtual arrays, so ordinary decoding
  // exercises jpeg_mem_available as well as the global allocation/free hooks.
  const auto reference = LibraryReference(jpeg, 1, &width, &height, &foreign);
  TEST_ASSERT_EQUAL_HEX32(0x13579bdfu, foreign.sentinel);
  TEST_ASSERT_EQUAL_UINT(73, width);
  TEST_ASSERT_EQUAL_UINT(65, height);
  uint32_t plain_width = 0, plain_height = 0;
  const auto plain = LibraryReference(jpeg, 1, &plain_width, &plain_height);
  TEST_ASSERT_EQUAL_UINT(width, plain_width);
  TEST_ASSERT_EQUAL_UINT(height, plain_height);
  TEST_ASSERT_EQUAL_UINT(reference.size(), plain.size());
  TEST_ASSERT_EQUAL_UINT8_ARRAY(plain.data(), reference.data(), reference.size());
}

static void test_progressive_rgb_colorspace_full_detail() {
  const auto encoded = MakeLibraryJpeg(73, 65, JpegFixtureFormat::kRgb);
  const auto adobe = JpegMarkerOffsets(encoded, 0xee);
  TEST_ASSERT_EQUAL_UINT(1, adobe.size());
  TEST_ASSERT_EQUAL_UINT8(0, encoded[adobe[0] + 15]);  // Adobe RGB transform.
  const size_t sof = FindJpegMarker(encoded, 0xc2);
  TEST_ASSERT_EQUAL_UINT8('R', encoded[sof + 10]);
  TEST_ASSERT_EQUAL_UINT8('G', encoded[sof + 13]);
  TEST_ASSERT_EQUAL_UINT8('B', encoded[sof + 16]);
  // Without APP14, the R/G/B component IDs still identify RGB colorspace.
  auto without_adobe = encoded;
  const size_t app14_length = (size_t{encoded[adobe[0] + 2]} << 8) |
                             encoded[adobe[0] + 3];
  without_adobe.erase(without_adobe.begin() + adobe[0],
                      without_adobe.begin() + adobe[0] + app14_length + 2);
  for (const auto *jpeg : std::array<const std::vector<uint8_t> *, 2>{&encoded, &without_adobe}) {
    artwork::ImageInfo info{};
    TEST_ASSERT_EQUAL_INT(static_cast<int>(artwork::DecodeStatus::kSuccess),
        static_cast<int>(artwork::InspectArtwork(jpeg->data(), jpeg->size(), &info)));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(artwork::JpegMode::kProgressive),
                          static_cast<int>(info.jpeg_mode));
    TEST_ASSERT_EQUAL_UINT(73, info.width);
    TEST_ASSERT_EQUAL_UINT(65, info.height);
    TEST_ASSERT_GREATER_THAN_UINT(1, JpegMarkerOffsets(*jpeg, 0xda).size());
    uint32_t width = 0, height = 0;
    const auto reference = LibraryReference(*jpeg, 1, &width, &height);
    TEST_ASSERT_TRUE(std::abs(int(reference[0]) - int(reference[2 * 3])) > 80);
    ProgressiveProbe probe;
    const auto options = ProgressiveOptions(&probe);
    pixoo_jpeg_statistics stats{};
    TEST_ASSERT_EQUAL_INT(PIXOO_JPEG_SUCCESS,
        DecodeProgressiveFixture(*jpeg, 73, 65, options, &stats));
    TEST_ASSERT_EQUAL_UINT(width, probe.width);
    TEST_ASSERT_EQUAL_UINT(height, probe.rows);
    TEST_ASSERT_EQUAL_UINT(reference.size(), probe.rgb.size());
    TEST_ASSERT_EQUAL_UINT8_ARRAY(reference.data(), probe.rgb.data(), reference.size());
    std::array<uint16_t, artwork::kArtworkPixelCount> output{};
    TEST_ASSERT_EQUAL_INT(static_cast<int>(artwork::DecodeStatus::kSuccess),
        static_cast<int>(artwork::DecodeArtwork(
            jpeg->data(), jpeg->size(), output.data(), output.size(), &info)));
    const auto expected = ReferenceArtwork(reference, width, height);
    TEST_ASSERT_EQUAL_HEX16_ARRAY(expected.data(), output.data(), output.size());
  }
}

static void test_progressive_inspector_scan_validation() {
  const auto jpeg = MakeLibraryJpeg(73, 65, JpegFixtureFormat::k420);
  const size_t sof = FindJpegMarker(jpeg, 0xc2);
  const auto scans = JpegMarkerOffsets(jpeg, 0xda);
  TEST_ASSERT_GREATER_THAN_UINT(2, scans.size());
  auto check = [](const std::vector<uint8_t> &input, artwork::DecodeStatus expected) {
    artwork::ImageInfo info{};
    TEST_ASSERT_EQUAL_INT(static_cast<int>(expected),
        static_cast<int>(artwork::InspectJpeg(input.data(), input.size(), &info)));
    AssertArtworkFailure(input, expected);
  };
  auto bad = jpeg;
  bad.push_back(0);
  check(bad, artwork::DecodeStatus::kMalformed);
  bad = jpeg;
  bad.resize(bad.size() - 2);
  check(bad, artwork::DecodeStatus::kIncomplete);
  bad = jpeg;
  bad.resize(scans[1] + 3);
  check(bad, artwork::DecodeStatus::kIncomplete);
  bad = jpeg;
  bad[scans[1] + 2] = 0x7f;
  check(bad, artwork::DecodeStatus::kIncomplete);
  bad = jpeg;
  bad[scans[1] + 3] = 2;
  check(bad, artwork::DecodeStatus::kMalformed);
  bad = jpeg;
  bad[sof + 1] = 0xc1;
  check(bad, artwork::DecodeStatus::kUnsupportedFormat);
  bad = jpeg;
  bad[sof + 4] = 12;
  check(bad, artwork::DecodeStatus::kUnsupportedFormat);
  bad = jpeg;
  bad[sof + 11] = 0; // Zero sampling factors cannot describe a component.
  check(bad, artwork::DecodeStatus::kMalformed);
  bad = jpeg;
  bad[sof + 12] = 4; // Quantization table selector is out of range.
  check(bad, artwork::DecodeStatus::kMalformed);
  bad = jpeg;
  const size_t frame_length = (size_t{jpeg[sof + 2]} << 8) | jpeg[sof + 3];
  bad.insert(bad.begin() + scans[0], jpeg.begin() + sof,
             jpeg.begin() + sof + 2 + frame_length);
  check(bad, artwork::DecodeStatus::kMalformed);
  bad = jpeg;
  bad[sof + 13] = bad[sof + 10]; // Duplicate frame component ID.
  check(bad, artwork::DecodeStatus::kMalformed);
  bad = jpeg;
  bad[scans[0] + 7] = bad[scans[0] + 5]; // Duplicate scan component ID.
  check(bad, artwork::DecodeStatus::kMalformed);
  bad = jpeg;
  bad[scans[1] + 5] = 99;
  check(bad, artwork::DecodeStatus::kMalformed);
  const size_t spectral = scans[1] + 5 + 2 * jpeg[scans[1] + 4];
  for (const auto values : {std::array<uint8_t, 3>{5, 4, 0},
                            std::array<uint8_t, 3>{1, 64, 0},
                            std::array<uint8_t, 3>{0, 63, 0},
                            std::array<uint8_t, 3>{1, 5, 0x31},
                            std::array<uint8_t, 3>{1, 5, 0xee},
                            std::array<uint8_t, 3>{1, 5, 0x10}}) {
    bad = jpeg;
    std::copy(values.begin(), values.end(), bad.begin() + spectral);
    check(bad, artwork::DecodeStatus::kMalformed);
  }
  bad = jpeg;
  const size_t dc_spectral = scans[0] + 5 + 2 * jpeg[scans[0] + 4];
  bad[dc_spectral + 1] = 1; // A DC scan cannot include AC coefficients.
  check(bad, artwork::DecodeStatus::kMalformed);
  bad = jpeg;
  bad[dc_spectral] = 1; // AC scans cannot interleave components.
  bad[dc_spectral + 1] = 5;
  check(bad, artwork::DecodeStatus::kMalformed);
  bad = jpeg;
  bad[scans[0] + 6] = 0x40; // Huffman table selector is out of range.
  check(bad, artwork::DecodeStatus::kMalformed);
  // Repeating a first DC scan without refinement violates coefficient history.
  bad = jpeg;
  const size_t first_length = (size_t{jpeg[scans[0] + 2]} << 8) | jpeg[scans[0] + 3];
  bad.insert(bad.begin() + scans[1], jpeg.begin() + scans[0],
             jpeg.begin() + scans[0] + 2 + first_length);
  check(bad, artwork::DecodeStatus::kMalformed);
  // Arithmetic coding and deferred dimensions are outside the supported modes.
  for (uint8_t marker : {uint8_t{0xcc}, uint8_t{0xdc}}) {
    bad = jpeg;
    const std::vector<uint8_t> segment{0xff, marker, 0, 4, 0, 0};
    bad.insert(bad.begin() + scans[0], segment.begin(), segment.end());
    check(bad, artwork::DecodeStatus::kUnsupportedFormat);
  }
}

static void test_progressive_entropy_corruption_and_cleanup() {
  const auto jpeg = MakeLibraryJpeg(73, 65, JpegFixtureFormat::k444);
  auto bad_huffman = jpeg;
  const auto tables = JpegMarkerOffsets(jpeg, 0xc4);
  TEST_ASSERT_FALSE(tables.empty());
  // An oversubscribed canonical Huffman tree: 255 one-bit symbols cannot fit.
  bad_huffman[tables[0] + 5] = 255;
  artwork::ImageInfo info{};
  TEST_ASSERT_EQUAL_INT(static_cast<int>(artwork::DecodeStatus::kSuccess),
      static_cast<int>(artwork::InspectJpeg(bad_huffman.data(), bad_huffman.size(), &info)));
  const auto scans = JpegMarkerOffsets(jpeg, 0xda);
  auto missing_entropy = jpeg;
  const size_t last = scans.back();
  const size_t length = (size_t{jpeg[last + 2]} << 8) | jpeg[last + 3];
  missing_entropy.erase(missing_entropy.begin() + last + 2 + length,
                         missing_entropy.end() - 2);
  for (unsigned repeat = 0; repeat < 8; ++repeat) {
    for (const auto *bad : {&bad_huffman, &missing_entropy}) {
      ProgressiveProbe probe;
      const auto options = ProgressiveOptions(&probe);
      pixoo_jpeg_statistics stats{};
      TEST_ASSERT_EQUAL_INT(PIXOO_JPEG_MALFORMED,
          DecodeProgressiveFixture(*bad, 73, 65, options, &stats));
      TEST_ASSERT_EQUAL_UINT(0, probe.rows);
      AssertArtworkFailure(*bad, artwork::DecodeStatus::kMalformed);
    }
    ProgressiveProbe probe;
    const auto options = ProgressiveOptions(&probe);
    pixoo_jpeg_statistics stats{};
    TEST_ASSERT_EQUAL_INT(PIXOO_JPEG_SUCCESS,
        DecodeProgressiveFixture(jpeg, 73, 65, options, &stats));
    TEST_ASSERT_EQUAL_UINT(65, probe.rows);
  }
  // Missing EOI is also a library failure when called without the inspector.
  auto truncated = jpeg;
  truncated.resize(truncated.size() - 2);
  ProgressiveProbe probe;
  const auto options = ProgressiveOptions(&probe);
  pixoo_jpeg_statistics stats{};
  TEST_ASSERT_EQUAL_INT(PIXOO_JPEG_MALFORMED,
      DecodeProgressiveFixture(truncated, 73, 65, options, &stats));
  auto trailing = jpeg;
  trailing.push_back(0);
  ProgressiveProbe trailing_probe;
  const auto trailing_options = ProgressiveOptions(&trailing_probe);
  TEST_ASSERT_EQUAL_INT(PIXOO_JPEG_MALFORMED,
      DecodeProgressiveFixture(trailing, 73, 65, trailing_options, &stats));
  // A late core failure can follow row emission; the public adapter must still
  // leave the caller's image untouched (its inspector rejects trailing bytes).
  TEST_ASSERT_EQUAL_UINT(65, trailing_probe.rows);
  AssertArtworkFailure(trailing, artwork::DecodeStatus::kMalformed);
}

static void test_progressive_restart_markers_and_scan_truncation() {
  const auto jpeg = MakeLibraryJpeg(97, 65, JpegFixtureFormat::k420, true, 3);
  TEST_ASSERT_LESS_THAN_UINT(jpeg.size(), FindJpegMarker(jpeg, 0xdd));
  const size_t restart = FindJpegMarker(jpeg, 0xd0);
  TEST_ASSERT_LESS_THAN_UINT(jpeg.size(), restart);
  ProgressiveProbe success;
  auto options = ProgressiveOptions(&success);
  pixoo_jpeg_statistics stats{};
  TEST_ASSERT_EQUAL_INT(PIXOO_JPEG_SUCCESS,
      DecodeProgressiveFixture(jpeg, 97, 65, options, &stats));
  uint32_t width = 0, height = 0;
  const auto reference = LibraryReference(jpeg, 1, &width, &height);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(reference.data(), success.rgb.data(), reference.size());
  const auto expected = ReferenceArtwork(reference, width, height);
  std::array<uint16_t, artwork::kArtworkPixelCount> output{};
  TEST_ASSERT_EQUAL_INT(static_cast<int>(artwork::DecodeStatus::kSuccess),
      static_cast<int>(artwork::DecodeArtwork(jpeg.data(), jpeg.size(), output.data(), output.size())));
  TEST_ASSERT_EQUAL_HEX16_ARRAY(expected.data(), output.data(), output.size());
  for (size_t scan : JpegMarkerOffsets(jpeg, 0xda)) {
    auto truncated = jpeg;
    truncated.resize(scan + 3); // Truncated length field at every scan, not only the first.
    ProgressiveProbe probe;
    options = ProgressiveOptions(&probe);
    TEST_ASSERT_EQUAL_INT(PIXOO_JPEG_MALFORMED,
        DecodeProgressiveFixture(truncated, 97, 65, options, &stats));
    TEST_ASSERT_EQUAL_UINT(0, probe.rows);
    AssertArtworkFailure(truncated, artwork::DecodeStatus::kIncomplete);
  }
  auto corrupt = jpeg;
  corrupt[restart + 1] = 0xd7; // Restart sequence must begin at RST0.
  ProgressiveProbe probe;
  options = ProgressiveOptions(&probe);
  TEST_ASSERT_EQUAL_INT(PIXOO_JPEG_MALFORMED,
      DecodeProgressiveFixture(corrupt, 97, 65, options, &stats));
  TEST_ASSERT_EQUAL_UINT(0, probe.rows);
  AssertArtworkFailure(corrupt, artwork::DecodeStatus::kMalformed);
}

static void test_progressive_memory_and_work_limits() {
  const auto jpeg = MakeLibraryJpeg(129, 131, JpegFixtureFormat::k420);
  ProgressiveProbe success;
  auto options = ProgressiveOptions(&success);
  pixoo_jpeg_statistics stats{};
  TEST_ASSERT_EQUAL_INT(PIXOO_JPEG_SUCCESS,
      DecodeProgressiveFixture(jpeg, 129, 131, options, &stats));
  const size_t peak = stats.peak_memory;
  for (const size_t budget : {size_t{1}, size_t{2048}, peak / 2, peak - 1}) {
    ProgressiveProbe probe;
    options = ProgressiveOptions(&probe);
    options.memory_limit = budget;
    TEST_ASSERT_EQUAL_INT(PIXOO_JPEG_MEMORY,
        DecodeProgressiveFixture(jpeg, 129, 131, options, &stats));
  }
  ProgressiveProbe exact;
  options = ProgressiveOptions(&exact);
  options.memory_limit = peak;
  TEST_ASSERT_EQUAL_INT(PIXOO_JPEG_SUCCESS,
      DecodeProgressiveFixture(jpeg, 129, 131, options, &stats));
  ProgressiveProbe clamped;
  options = ProgressiveOptions(&clamped);
  options.memory_limit = PIXOO_JPEG_MEMORY_LIMIT + size_t{1};
  options.scan_limit = PIXOO_JPEG_SCAN_LIMIT + 1;
  options.time_limit_ms = 10001;
  TEST_ASSERT_EQUAL_INT(PIXOO_JPEG_SUCCESS,
      DecodeProgressiveFixture(jpeg, 129, 131, options, &stats));
  TEST_ASSERT_EQUAL_UINT(peak, stats.peak_memory);
  ProgressiveProbe limited;
  options = ProgressiveOptions(&limited);
  options.scan_limit = 1;
  TEST_ASSERT_EQUAL_INT(PIXOO_JPEG_WORK_LIMIT,
      DecodeProgressiveFixture(jpeg, 129, 131, options, &stats));
  TEST_ASSERT_EQUAL_UINT(0, limited.rows);
  ProgressiveProbe timed;
  timed.delay_once = true;
  options = ProgressiveOptions(&timed);
  options.time_limit_ms = 1;
  TEST_ASSERT_EQUAL_INT(PIXOO_JPEG_WORK_LIMIT,
      DecodeProgressiveFixture(jpeg, 129, 131, options, &stats));
  TEST_ASSERT_EQUAL_UINT(0, timed.rows);
  // Scaled output is small, but full-resolution coefficients exceed the cap.
  const auto large = MakeLibraryJpeg(2049, 2049, JpegFixtureFormat::kGray);
  TEST_ASSERT_LESS_THAN_UINT(artwork::kMaxEncodedBytes, large.size());
  ProgressiveProbe large_probe;
  options = ProgressiveOptions(&large_probe);
  TEST_ASSERT_EQUAL_INT(PIXOO_JPEG_MEMORY,
      DecodeProgressiveFixture(large, 2049, 2049, options, &stats));
  AssertArtworkFailure(large, artwork::DecodeStatus::kOutOfMemory);
  // The inspector's scan bound is independent of the entropy decoder. All
  // preceding scan headers have legal, non-overlapping coefficient history.
  std::vector<uint8_t> many{0xff, 0xd8};
  AppendJpegSegment(&many, 0xc2, {8, 0, 8, 0, 8, 1, 1, 0x11, 0});
  AppendJpegSegment(&many, 0xda, {1, 1, 0, 0, 0, 2});
  many.push_back(0);
  for (uint8_t c = 1; c <= 63; ++c) {
    AppendJpegSegment(&many, 0xda, {1, 1, 0, c, c, 0});
    many.push_back(0);
  }
  AppendJpegSegment(&many, 0xda, {1, 1, 0, 0, 0, 0x21});
  many.insert(many.end(), {0, 0xff, 0xd9});
  AssertArtworkFailure(many, artwork::DecodeStatus::kWorkLimitExceeded);
}

static void test_progressive_cancellation_scan_and_row_output() {
  const auto jpeg = MakeLibraryJpeg(129, 131, JpegFixtureFormat::k422);
  pixoo_jpeg_statistics stats{};
  for (unsigned at : {1u, 4u, 12u}) {
    ProgressiveProbe probe;
    probe.cancel_at = at;
    const auto options = ProgressiveOptions(&probe);
    TEST_ASSERT_EQUAL_INT(PIXOO_JPEG_CANCELLED,
        DecodeProgressiveFixture(jpeg, 129, 131, options, &stats));
    TEST_ASSERT_EQUAL_UINT(at, probe.calls);
    TEST_ASSERT_EQUAL_UINT(0, probe.rows); // Cancellation during scan consumption.
    CancellationProbe adapter{0, at};
    AssertArtworkFailure(jpeg, artwork::DecodeStatus::kCancelled, &adapter);
  }
  ProgressiveProbe rows;
  rows.cancel_after_rows = 3;
  auto options = ProgressiveOptions(&rows);
  TEST_ASSERT_EQUAL_INT(PIXOO_JPEG_CANCELLED,
      DecodeProgressiveFixture(jpeg, 129, 131, options, &stats));
  TEST_ASSERT_EQUAL_UINT(3, rows.rows);
  TEST_ASSERT_GREATER_THAN_UINT(1, stats.scans);
  // The adapter checks twice per 65-pixel output row as well as the core's
  // checkpoints. A completed dry run supplies a stable late-output threshold.
  std::array<uint16_t, artwork::kArtworkPixelCount> output{};
  CancellationProbe count{0, size_t(-1)};
  TEST_ASSERT_EQUAL_INT(static_cast<int>(artwork::DecodeStatus::kSuccess),
      static_cast<int>(artwork::DecodeArtwork(jpeg.data(), jpeg.size(),
          output.data(), output.size(), nullptr, CancelAtCall, &count)));
  CancellationProbe late{0, count.calls - 10};
  AssertArtworkFailure(jpeg, artwork::DecodeStatus::kCancelled, &late);
  ProgressiveProbe rejected;
  rejected.reject_row = true;
  options = ProgressiveOptions(&rejected);
  TEST_ASSERT_EQUAL_INT(PIXOO_JPEG_OUTPUT_FAILED,
      DecodeProgressiveFixture(jpeg, 129, 131, options, &stats));
  TEST_ASSERT_EQUAL_UINT(1, rejected.rows);
  auto incomplete = jpeg;
  incomplete.pop_back();
  CancellationProbe validation_first{0, 1};
  AssertArtworkFailure(incomplete, artwork::DecodeStatus::kIncomplete, &validation_first);
  TEST_ASSERT_EQUAL_UINT(0, validation_first.calls);
}

static void test_progressive_internal_invalid_and_unsupported_modes() {
  pixoo_jpeg_statistics stats{};
  for (const auto format : {JpegFixtureFormat::kGray, JpegFixtureFormat::kCmyk}) {
    const bool progressive = format != JpegFixtureFormat::kGray;
    const auto jpeg = MakeLibraryJpeg(65, 67, format, progressive);
    ProgressiveProbe probe;
    const auto options = ProgressiveOptions(&probe);
    TEST_ASSERT_EQUAL_INT(PIXOO_JPEG_INVALID,
        DecodeProgressiveFixture(jpeg, 65, 67, options, &stats));
    TEST_ASSERT_EQUAL_UINT(0, probe.rows);
    if (format == JpegFixtureFormat::kCmyk)
      AssertArtworkFailure(jpeg, artwork::DecodeStatus::kUnsupportedFormat);
  }
  const auto jpeg = MakeLibraryJpeg(65, 67, JpegFixtureFormat::kGray);
  ProgressiveProbe probe;
  auto options = ProgressiveOptions(&probe);
  TEST_ASSERT_EQUAL_INT(PIXOO_JPEG_INVALID,
      DecodeProgressiveFixture(jpeg, 66, 67, options, &stats));
  for (const uint32_t width : {0u, 4097u}) {
    TEST_ASSERT_EQUAL_INT(PIXOO_JPEG_INVALID,
        DecodeProgressiveFixture(jpeg, width, 67, options, &stats));
  }
  options.row = nullptr;
  TEST_ASSERT_EQUAL_INT(PIXOO_JPEG_INVALID,
      DecodeProgressiveFixture(jpeg, 65, 67, options, &stats));
  options = ProgressiveOptions(&probe);
  for (const size_t size : {size_t{0}, size_t{512 * 1024 + 1}}) {
    TEST_ASSERT_EQUAL_INT(PIXOO_JPEG_INVALID, pixoo_decode_progressive_jpeg(
        jpeg.data(), size, 65, 67, &options, &stats));
    TEST_ASSERT_EQUAL_UINT(0, stats.live_memory);
  }
  TEST_ASSERT_EQUAL_INT(PIXOO_JPEG_INVALID, pixoo_decode_progressive_jpeg(
      nullptr, jpeg.size(), 65, 67, &options, &stats));
  TEST_ASSERT_EQUAL_UINT(0, stats.live_memory);
  TEST_ASSERT_EQUAL_INT(PIXOO_JPEG_INVALID, pixoo_decode_progressive_jpeg(
      jpeg.data(), jpeg.size(), 65, 67, nullptr, &stats));
  TEST_ASSERT_EQUAL_UINT(0, stats.live_memory);
}

static void test_record_and_redaction() {
  static_assert(std::is_trivially_copyable<cfg::ConfigRecord>::value, "preference record");
  static_assert(sizeof(cfg::ConfigRecord::entity_id) == 97, "entity field");
  static_assert(sizeof(cfg::ConfigRecord::home_assistant_url) == 257, "URL field");
  cfg::ConfigRecord record{};
  TEST_ASSERT_TRUE(cfg::MakeConfigRecord(
      "media_player.fixture_room", std::strlen("media_player.fixture_room"),
      "https://panel.invalid/", 22, 17, &record));
  TEST_ASSERT_TRUE(cfg::ValidateConfigRecord(record));
  TEST_ASSERT_EQUAL_UINT8(cfg::kConfigFormatVersion, record.format_version);
  TEST_ASSERT_EQUAL_UINT8(cfg::kConfigValidMarker, record.valid_marker);
  TEST_ASSERT_EQUAL_UINT32(17, record.revision);
  TEST_ASSERT_EQUAL_STRING("https://panel.invalid", record.home_assistant_url);
  TEST_ASSERT_EQUAL_STRING("https://panel.invalid/path?<redacted>",
                           cfg::RedactUrlForDiagnostics("https://panel.invalid/path?secret=value", 39).c_str());
  TEST_ASSERT_EQUAL_STRING("<redacted>",
                           cfg::RedactUrlForDiagnostics("https://user@panel.invalid/path", 31).c_str());
}

namespace esphome::pixoo64_render_test {

int RunNowPlayingAdapterTests() {
  UNITY_BEGIN();
  RUN_TEST(test_entity_ids);
  RUN_TEST(test_home_assistant_urls);
  RUN_TEST(test_artwork_urls);
  RUN_TEST(test_artwork_fetch_policy_and_magic);
  RUN_TEST(test_artwork_crop_mapping_and_color_math);
  RUN_TEST(test_png_chunk_validation);
  RUN_TEST(test_jpeg_marker_validation);
  RUN_TEST(test_actual_png_decode_area_resampling);
  RUN_TEST(test_png_decode_cancellation);
  RUN_TEST(test_actual_baseline_jpeg_decode_and_rejections);
  RUN_TEST(test_jpeg_reduced_decode_callback_tiling);
  RUN_TEST(test_jpeg_decode_cancellation);
  RUN_TEST(test_progressive_final_image_formats_scaling_and_detail);
  RUN_TEST(test_libjpeg_foreign_client_data);
  RUN_TEST(test_progressive_rgb_colorspace_full_detail);
  RUN_TEST(test_progressive_inspector_scan_validation);
  RUN_TEST(test_progressive_entropy_corruption_and_cleanup);
  RUN_TEST(test_progressive_restart_markers_and_scan_truncation);
  RUN_TEST(test_progressive_memory_and_work_limits);
  RUN_TEST(test_progressive_cancellation_scan_and_row_output);
  RUN_TEST(test_progressive_internal_invalid_and_unsupported_modes);
  RUN_TEST(test_record_and_redaction);
  return UNITY_END();
}

}  // namespace esphome::pixoo64_render_test

#endif  // USE_PIXOO64_NOW_PLAYING
