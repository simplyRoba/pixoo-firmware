#pragma once

#include <cstddef>
#include <cstdint>

#include "now_playing_data.h"

namespace pixoo::now_playing {

struct TextSanitizeResult {
  uint16_t byte_count{0};
  bool replaced{false};
  bool truncated{false};
};

bool IsPixelOperatorGlyph(uint32_t codepoint);

// Converts malformed UTF-8 and unmapped code points to '?'. Output is always
// terminated, and is cut only before a complete encoded output code point.
TextSanitizeResult SanitizeNowPlayingText(const char *input, size_t input_size,
                                          BoundedText *output);

}  // namespace pixoo::now_playing
