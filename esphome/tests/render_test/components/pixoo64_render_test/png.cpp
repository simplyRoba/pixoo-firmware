#include "png.h"

namespace esphome::pixoo64_render_test {
namespace {

uint32_t Crc32(const uint8_t *data, size_t len, uint32_t crc = 0) {
  crc = ~crc;
  for (size_t i = 0; i < len; ++i) {
    crc ^= data[i];
    for (int b = 0; b < 8; ++b)
      crc = (crc >> 1) ^ (0xEDB88320u & (~(crc & 1) + 1));
  }
  return ~crc;
}

uint32_t Adler32(const uint8_t *data, size_t len) {
  uint32_t a = 1, b = 0;
  for (size_t i = 0; i < len; ++i) {
    a = (a + data[i]) % 65521;
    b = (b + a) % 65521;
  }
  return (b << 16) | a;
}

void PushBE32(std::vector<uint8_t> &out, uint32_t v) {
  out.push_back(v >> 24);
  out.push_back((v >> 16) & 0xFF);
  out.push_back((v >> 8) & 0xFF);
  out.push_back(v & 0xFF);
}

void PushChunk(std::vector<uint8_t> &out, const char tag[4],
               const std::vector<uint8_t> &data) {
  PushBE32(out, static_cast<uint32_t>(data.size()));
  const size_t tag_at = out.size();
  for (int i = 0; i < 4; ++i) out.push_back(static_cast<uint8_t>(tag[i]));
  out.insert(out.end(), data.begin(), data.end());
  const uint32_t crc = Crc32(&out[tag_at], 4 + data.size());
  PushBE32(out, crc);
}

}  // namespace

std::vector<uint8_t> EncodePng(const uint8_t *rgb, int width, int height) {
  std::vector<uint8_t> out = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};

  std::vector<uint8_t> ihdr;
  PushBE32(ihdr, static_cast<uint32_t>(width));
  PushBE32(ihdr, static_cast<uint32_t>(height));
  ihdr.push_back(8);  // bit depth
  ihdr.push_back(2);  // color type: truecolor RGB
  ihdr.push_back(0);  // compression
  ihdr.push_back(0);  // filter
  ihdr.push_back(0);  // interlace
  PushChunk(out, "IHDR", ihdr);

  // Raw scanlines: one filter byte (0 = none) per row, then RGB pixels.
  std::vector<uint8_t> raw;
  raw.reserve(static_cast<size_t>(height) * (1 + width * 3));
  for (int y = 0; y < height; ++y) {
    raw.push_back(0);
    const uint8_t *row = rgb + static_cast<size_t>(y) * width * 3;
    raw.insert(raw.end(), row, row + static_cast<size_t>(width) * 3);
  }

  // zlib stream: header + stored (uncompressed) deflate blocks + adler32.
  std::vector<uint8_t> zlib = {0x78, 0x01};
  size_t offset = 0;
  while (offset < raw.size()) {
    const size_t block = raw.size() - offset > 65535 ? 65535 : raw.size() - offset;
    const bool final = offset + block == raw.size();
    zlib.push_back(final ? 1 : 0);
    zlib.push_back(block & 0xFF);
    zlib.push_back((block >> 8) & 0xFF);
    zlib.push_back(~block & 0xFF);
    zlib.push_back((~block >> 8) & 0xFF);
    zlib.insert(zlib.end(), raw.begin() + offset, raw.begin() + offset + block);
    offset += block;
  }
  PushBE32(zlib, Adler32(raw.data(), raw.size()));
  PushChunk(out, "IDAT", zlib);

  PushChunk(out, "IEND", {});
  return out;
}

}  // namespace esphome::pixoo64_render_test
