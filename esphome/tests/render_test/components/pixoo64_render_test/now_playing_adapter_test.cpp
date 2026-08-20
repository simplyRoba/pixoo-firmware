#ifdef USE_PIXOO64_NOW_PLAYING

#include "now_playing_adapter_test.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <string>
#include <type_traits>
#include <vector>

#include <unity.h>

extern "C" void setUp() {}
extern "C" void tearDown() {}

#include "now_playing_config.h"
#include "artwork_decoder.h"
#include "artwork_fetch_policy.h"

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

// Original fixture encoder: grayscale baseline JPEG with constant 8x8 MCUs.
// Edge MCUs are deliberately bright/dark so the decoder test proves the
// centered 64-pixel crop excludes them without using third-party artwork.
std::vector<uint8_t> MakeBaselineJpeg() {
  constexpr uint16_t width = 128;
  constexpr uint16_t height = 64;
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
  for (uint16_t block_y = 0; block_y < height / 8; ++block_y) {
    (void) block_y;
    for (uint16_t block_x = 0; block_x < width / 8; ++block_x) {
      const int sample = block_x < 4 ? 10 : block_x < 8 ? 80
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

struct CancellationProbe {
  size_t calls{0};
  size_t cancel_at{0};
};

bool CancelAtCall(void *opaque) {
  auto *probe = static_cast<CancellationProbe *>(opaque);
  ++probe->calls;
  return probe->calls >= probe->cancel_at;
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
  TEST_ASSERT_TRUE(artwork::AcceptBodySize(artwork::kMaxEncodedBytes, false));
  TEST_ASSERT_FALSE(artwork::AcceptBodySize(artwork::kMaxEncodedBytes + 1, false));
  TEST_ASSERT_TRUE(artwork::AcceptBodySize(artwork::kMaxEncodedBytes + 1, true));
  TEST_ASSERT_TRUE(artwork::IsCompleteBody(4, 4, false, true));
  TEST_ASSERT_FALSE(artwork::IsCompleteBody(3, 4, false, true));
  TEST_ASSERT_FALSE(artwork::IsCompleteBody(4, 4, true, false));
  TEST_ASSERT_FALSE(artwork::IsCompleteBody(artwork::kMaxEncodedBytes + 1, 0, true, true));
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

  for (uint32_t extent : {1u, 2u, 63u, 64u, 65u, 96u, 4096u}) {
    uint32_t previous = 17;
    for (uint32_t destination = 0; destination < 64; ++destination) {
      const uint32_t mapped = artwork::MapDestinationCoordinate(
          destination, 17, extent);
      TEST_ASSERT_GREATER_OR_EQUAL_UINT32(17, mapped);
      TEST_ASSERT_LESS_THAN_UINT32(17 + extent, mapped);
      if (destination != 0)
        TEST_ASSERT_GREATER_OR_EQUAL_UINT32(previous, mapped);
      previous = mapped;
    }
  }
  TEST_ASSERT_EQUAL_UINT32(16,
      artwork::MapDestinationCoordinate(0, 16, 96));
  TEST_ASSERT_EQUAL_UINT32(111,
      artwork::MapDestinationCoordinate(63, 16, 96));

  TEST_ASSERT_EQUAL_HEX16(0xf800, artwork::Rgb888ToRgb565(255, 0, 0));
  TEST_ASSERT_EQUAL_HEX16(0x07e0, artwork::Rgb888ToRgb565(0, 255, 0));
  TEST_ASSERT_EQUAL_HEX16(0x001f, artwork::Rgb888ToRgb565(0, 0, 255));
  TEST_ASSERT_EQUAL_HEX16(0x8000,
      artwork::CompositeRgbaOverRgb565(255, 0, 0, 128, 0x0000));
  TEST_ASSERT_EQUAL_HEX16(0xffff,
      artwork::CompositeRgbaOverRgb565(0, 0, 0, 0, 0xffff));
  TEST_ASSERT_EQUAL_HEX16(artwork::Rgb888ToRgb565(1, 2, 3),
      artwork::CompositeRgbaOverRgb565(1, 2, 3, 255, 0xffff));
}

static void test_png_chunk_validation() {
  std::vector<uint8_t> pixels(2 * 2 * 4, 0xff);
  const std::vector<uint8_t> png = MakeRgbaPng(2, 2, pixels);
  artwork::ImageInfo info{};
  TEST_ASSERT_EQUAL_INT(static_cast<int>(artwork::DecodeStatus::kSuccess),
      static_cast<int>(artwork::InspectPng(png.data(), png.size(), &info)));
  TEST_ASSERT_EQUAL_UINT32(2, info.width);
  TEST_ASSERT_EQUAL_UINT32(2, info.height);
  TEST_ASSERT_TRUE(info.has_alpha);
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(artwork::DecodeStatus::kEncodedTooLarge),
      static_cast<int>(artwork::InspectPng(
          png.data(), artwork::kMaxEncodedBytes + 1, &info)));

  const std::vector<uint8_t> animated = MakeRgbaPng(2, 2, pixels, false, true);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(artwork::DecodeStatus::kAnimatedPng),
      static_cast<int>(artwork::InspectPng(animated.data(), animated.size(), &info)));
  const std::vector<uint8_t> interlaced = MakeRgbaPng(2, 2, pixels, true);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(artwork::DecodeStatus::kInterlacedPng),
      static_cast<int>(artwork::InspectPng(interlaced.data(), interlaced.size(), &info)));

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

  std::vector<uint8_t> oversized_pixels(4097 * 4, 0xff);
  const std::vector<uint8_t> oversized =
      MakeRgbaPng(4097, 1, oversized_pixels);
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
  std::vector<uint8_t> progressive = jpeg;
  progressive[sof + 1] = 0xc2;
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(artwork::DecodeStatus::kProgressiveJpeg),
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

static void test_actual_png_decode_crop_scale_and_alpha() {
  std::array<uint16_t, artwork::kArtworkPixelCount> placeholder{};
  std::array<uint16_t, artwork::kArtworkPixelCount> output{};
  TEST_ASSERT_TRUE(artwork::GenerateArtworkPlaceholder(
      0x99887766, placeholder.data(), placeholder.size()));

  const std::vector<uint8_t> alpha_pixels{
      255, 0, 0, 0,       0, 255, 0, 255,
      0, 0, 255, 128,     255, 255, 255, 255,
  };
  const std::vector<uint8_t> alpha_png = MakeRgbaPng(2, 2, alpha_pixels);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(artwork::DecodeStatus::kSuccess),
      static_cast<int>(artwork::DecodeArtwork(
          alpha_png.data(), alpha_png.size(), 1, placeholder.data(),
          output.data(), output.size())));
  TEST_ASSERT_EQUAL_HEX16(placeholder[0], output[0]);
  TEST_ASSERT_EQUAL_HEX16(0x07e0, output[63]);
  TEST_ASSERT_EQUAL_HEX16(
      artwork::CompositeRgbaOverRgb565(0, 0, 255, 128,
                                       placeholder[63 * 64]),
      output[63 * 64]);
  TEST_ASSERT_EQUAL_HEX16(0xffff, output[63 * 64 + 63]);

  std::array<uint16_t, artwork::kArtworkPixelCount> url_seed_a{};
  std::array<uint16_t, artwork::kArtworkPixelCount> url_seed_b{};
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(artwork::DecodeStatus::kSuccess),
      static_cast<int>(artwork::DecodeArtwork(
          alpha_png.data(), alpha_png.size(), 11, nullptr, url_seed_a.data(),
          url_seed_a.size())));
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(artwork::DecodeStatus::kSuccess),
      static_cast<int>(artwork::DecodeArtwork(
          alpha_png.data(), alpha_png.size(), 12, nullptr, url_seed_b.data(),
          url_seed_b.size())));
  TEST_ASSERT_NOT_EQUAL(
      0, std::memcmp(url_seed_a.data(), url_seed_b.data(),
                     artwork::kArtworkRgb565Bytes));
  const uint64_t body_seed = artwork::EncodedBodyPlaceholderSeed(
      alpha_png.data(), alpha_png.size());
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(artwork::DecodeStatus::kSuccess),
      static_cast<int>(artwork::DecodeArtwork(
          alpha_png.data(), alpha_png.size(), body_seed, nullptr,
          url_seed_a.data(), url_seed_a.size())));
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(artwork::DecodeStatus::kSuccess),
      static_cast<int>(artwork::DecodeArtwork(
          alpha_png.data(), alpha_png.size(), body_seed, nullptr,
          url_seed_b.data(), url_seed_b.size())));
  TEST_ASSERT_EQUAL_MEMORY(url_seed_a.data(), url_seed_b.data(),
                           artwork::kArtworkRgb565Bytes);

  constexpr uint32_t width = 128;
  constexpr uint32_t height = 96;
  std::vector<uint8_t> crop_pixels(width * height * 4);
  for (uint32_t y = 0; y < height; ++y) {
    for (uint32_t x = 0; x < width; ++x) {
      const size_t index = (static_cast<size_t>(y) * width + x) * 4;
      uint8_t red = 255;
      uint8_t green = 0;
      uint8_t blue = 0;
      if (x >= 16 && x <= 111) {
        red = 0;
        green = 255;
      }
      if (x == 111) {
        green = 0;
        blue = 255;
      }
      crop_pixels[index + 0] = red;
      crop_pixels[index + 1] = green;
      crop_pixels[index + 2] = blue;
      crop_pixels[index + 3] = 255;
    }
  }
  const std::vector<uint8_t> crop_png =
      MakeRgbaPng(width, height, crop_pixels);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(artwork::DecodeStatus::kSuccess),
      static_cast<int>(artwork::DecodeArtwork(
          crop_png.data(), crop_png.size(), 2, nullptr, output.data(),
          output.size())));
  TEST_ASSERT_EQUAL_HEX16(0x07e0, output[0]);
  TEST_ASSERT_EQUAL_HEX16(0x001f, output[63]);
  for (uint16_t pixel : output)
    TEST_ASSERT_NOT_EQUAL_HEX16(0xf800, pixel);
  std::array<uint16_t, artwork::kArtworkPixelCount> other_identity{};
  TEST_ASSERT_EQUAL_INT(static_cast<int>(artwork::DecodeStatus::kSuccess),
      static_cast<int>(artwork::DecodeArtwork(
          crop_png.data(), crop_png.size(), 0xffffffffffffffffull, nullptr,
          other_identity.data(), other_identity.size())));
  TEST_ASSERT_EQUAL_MEMORY(output.data(), other_identity.data(),
                           artwork::kArtworkRgb565Bytes);

  std::vector<uint8_t> incomplete = crop_png;
  incomplete.pop_back();
  TEST_ASSERT_EQUAL_INT(static_cast<int>(artwork::DecodeStatus::kIncomplete),
      static_cast<int>(artwork::DecodeArtwork(
          incomplete.data(), incomplete.size(), 2, nullptr, output.data(),
          output.size())));
}

static void test_png_decode_cancellation() {
  constexpr uint32_t width = 64;
  constexpr uint32_t height = 64;
  std::vector<uint8_t> pixels(width * height * 4, 0);
  for (size_t i = 0; i < pixels.size(); i += 4) {
    pixels[i] = 255;
    pixels[i + 3] = 255;
  }
  const std::vector<uint8_t> png = MakeRgbaPng(width, height, pixels);
  std::array<uint16_t, artwork::kArtworkPixelCount> output{};
  output.fill(0x5a5a);

  artwork::ImageInfo info{};
  CancellationProbe immediate{0, 1};
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(artwork::DecodeStatus::kCancelled),
      static_cast<int>(artwork::DecodeArtwork(
          png.data(), png.size(), 0, nullptr, output.data(), output.size(),
          &info, CancelAtCall, &immediate)));
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
          incomplete.data(), incomplete.size(), 0, nullptr, output.data(),
          output.size(), nullptr, CancelAtCall, &validation_first)));
  TEST_ASSERT_EQUAL_UINT(0, validation_first.calls);

  std::array<uint16_t, artwork::kArtworkPixelCount> transparent_placeholder{};
  output.fill(0x5a5a);
  CancellationProbe after_draws{0, 75};
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(artwork::DecodeStatus::kCancelled),
      static_cast<int>(artwork::DecodeArtwork(
          png.data(), png.size(), 0, transparent_placeholder.data(),
          output.data(), output.size(), nullptr, CancelAtCall,
          &after_draws)));
  TEST_ASSERT_EQUAL_UINT(75, after_draws.calls);
  const size_t red_pixels = static_cast<size_t>(std::count(
      output.begin(), output.end(), artwork::Rgb888ToRgb565(255, 0, 0)));
  TEST_ASSERT_GREATER_THAN_UINT(0, red_pixels);
  TEST_ASSERT_LESS_THAN_UINT(artwork::kArtworkPixelCount, red_pixels);
}

static void test_actual_baseline_jpeg_decode_and_rejections() {
  const std::vector<uint8_t> jpeg = MakeBaselineJpeg();
  std::array<uint16_t, artwork::kArtworkPixelCount> output{};
  artwork::ImageInfo info{};
  TEST_ASSERT_EQUAL_INT(static_cast<int>(artwork::DecodeStatus::kSuccess),
      static_cast<int>(artwork::DecodeArtwork(
          jpeg.data(), jpeg.size(), 0, nullptr, output.data(), output.size(),
          &info)));
  TEST_ASSERT_EQUAL_UINT32(128, info.width);
  TEST_ASSERT_EQUAL_UINT32(64, info.height);
  TEST_ASSERT_UINT8_WITHIN(8, 80, RedFrom565(output[0]));
  TEST_ASSERT_UINT8_WITHIN(8, 80, GreenFrom565(output[0]));
  TEST_ASSERT_UINT8_WITHIN(8, 80, BlueFrom565(output[0]));
  TEST_ASSERT_UINT8_WITHIN(8, 180, RedFrom565(output[63]));
  TEST_ASSERT_UINT8_WITHIN(8, 180, GreenFrom565(output[63]));
  TEST_ASSERT_UINT8_WITHIN(8, 180, BlueFrom565(output[63]));

  std::vector<uint8_t> progressive = jpeg;
  const size_t sof = FindJpegMarker(progressive, 0xc0);
  progressive[sof + 1] = 0xc2;
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(artwork::DecodeStatus::kProgressiveJpeg),
      static_cast<int>(artwork::DecodeArtwork(
          progressive.data(), progressive.size(), 0, nullptr, output.data(),
          output.size())));
  std::vector<uint8_t> incomplete = jpeg;
  incomplete.pop_back();
  TEST_ASSERT_EQUAL_INT(static_cast<int>(artwork::DecodeStatus::kIncomplete),
      static_cast<int>(artwork::DecodeArtwork(
          incomplete.data(), incomplete.size(), 0, nullptr, output.data(),
          output.size())));
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
          jpeg.data(), jpeg.size(), 0, nullptr, output.data(), output.size(),
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
          jpeg.data(), jpeg.size(), 0, nullptr, output.data(), output.size(),
          nullptr, CancelAtCall, &after_draws)));
  TEST_ASSERT_EQUAL_UINT(13, after_draws.calls);
  const size_t nonzero = static_cast<size_t>(
      std::count_if(output.begin(), output.end(),
                    [](uint16_t pixel) { return pixel != 0; }));
  TEST_ASSERT_GREATER_THAN_UINT(0, nonzero);
  TEST_ASSERT_LESS_THAN_UINT(artwork::kArtworkPixelCount, nonzero);
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
  RUN_TEST(test_actual_png_decode_crop_scale_and_alpha);
  RUN_TEST(test_png_decode_cancellation);
  RUN_TEST(test_actual_baseline_jpeg_decode_and_rejections);
  RUN_TEST(test_jpeg_decode_cancellation);
  RUN_TEST(test_record_and_redaction);
  return UNITY_END();
}

}  // namespace esphome::pixoo64_render_test

#endif  // USE_PIXOO64_NOW_PLAYING
