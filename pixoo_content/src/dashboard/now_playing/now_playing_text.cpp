#include "now_playing_text.h"

namespace pixoo::now_playing {
namespace {
bool InRanges(uint32_t c) {
  static constexpr struct { uint16_t first, last; } ranges[] = {
      {0x20,0x7e},{0xa0,0xa3},{0xa5,0xa6},{0xa8,0xa9},{0xab,0xac},
      {0xae,0xae},{0xb0,0xb1},{0xb4,0xb8},{0xbb,0xbb},{0xbf,0xff},
      {0x108,0x109},{0x10c,0x10f},{0x11a,0x11d},{0x124,0x125},
      {0x131,0x131},{0x134,0x135},{0x147,0x148},{0x152,0x153},
      {0x158,0x159},{0x15c,0x15d},{0x160,0x161},{0x164,0x165},
      {0x16c,0x16f},{0x178,0x178},{0x17d,0x17e},{0x2c6,0x2c7},
      {0x2d8,0x2d8},{0x2da,0x2da},{0x2dc,0x2dc},{0x2013,0x2014},
      {0x2018,0x201a},{0x201c,0x201e},{0x2020,0x2022},{0x2026,0x2026},
      {0x2030,0x2030},{0x2039,0x203a},{0x20ac,0x20ac},{0x20b1,0x20b1},
      {0x20b7,0x20b7},{0x2117,0x2117},{0x2122,0x2122},
  };
  for (const auto &range : ranges) if (c >= range.first && c <= range.last) return true;
  return false;
}

size_t Encode(uint32_t c, char out[4]) {
  if (c <= 0x7f) { out[0] = static_cast<char>(c); return 1; }
  if (c <= 0x7ff) { out[0] = static_cast<char>(0xc0 | (c >> 6)); out[1] = static_cast<char>(0x80 | (c & 0x3f)); return 2; }
  if (c <= 0xffff) { out[0] = static_cast<char>(0xe0 | (c >> 12)); out[1] = static_cast<char>(0x80 | ((c >> 6) & 0x3f)); out[2] = static_cast<char>(0x80 | (c & 0x3f)); return 3; }
  out[0] = static_cast<char>(0xf0 | (c >> 18)); out[1] = static_cast<char>(0x80 | ((c >> 12) & 0x3f)); out[2] = static_cast<char>(0x80 | ((c >> 6) & 0x3f)); out[3] = static_cast<char>(0x80 | (c & 0x3f)); return 4;
}

bool Decode(const unsigned char *in, size_t available, uint32_t *codepoint, size_t *used) {
  const unsigned char first = in[0];
  if (first < 0x80) { *codepoint = first; *used = 1; return true; }
  size_t count = 0; uint32_t cp = 0; uint32_t minimum = 0;
  if (first >= 0xc2 && first <= 0xdf) { count = 2; cp = first & 0x1f; minimum = 0x80; }
  else if (first >= 0xe0 && first <= 0xef) { count = 3; cp = first & 0x0f; minimum = 0x800; }
  else if (first >= 0xf0 && first <= 0xf4) { count = 4; cp = first & 0x07; minimum = 0x10000; }
  else { *used = 1; return false; }
  if (available < count) { *used = 1; return false; }
  for (size_t i = 1; i < count; ++i) {
    if ((in[i] & 0xc0) != 0x80) { *used = 1; return false; }
    cp = (cp << 6) | (in[i] & 0x3f);
  }
  if (cp < minimum || cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff)) { *used = 1; return false; }
  *codepoint = cp; *used = count; return true;
}
}  // namespace

bool IsPixelOperatorGlyph(uint32_t codepoint) { return InRanges(codepoint); }

TextSanitizeResult SanitizeNowPlayingText(const char *input, size_t input_size,
                                          BoundedText *output) {
  TextSanitizeResult result{};
  if (output == nullptr) return result;
  output->size = 0; output->bytes[0] = '\0';
  if (input == nullptr) input_size = 0;
  size_t offset = 0;
  while (offset < input_size) {
    uint32_t codepoint = 0; size_t used = 1;
    bool valid = Decode(reinterpret_cast<const unsigned char *>(input + offset), input_size - offset, &codepoint, &used);
    if (!valid || !IsPixelOperatorGlyph(codepoint)) { codepoint = '?'; result.replaced = true; }
    char encoded[4]; const size_t count = Encode(codepoint, encoded);
    if (output->size + count > BoundedText::kMaxBytes) { result.truncated = true; break; }
    for (size_t i = 0; i < count; ++i) output->bytes[output->size++] = encoded[i];
    offset += used;
  }
  output->bytes[output->size] = '\0'; result.byte_count = output->size;
  return result;
}

}  // namespace pixoo::now_playing
