#pragma once

#include <cstddef>
#include <cstdint>

#include "now_playing_data.h"

namespace pixoo::now_playing {

constexpr size_t kNowPlayingArtworkWidth = 64;
constexpr size_t kNowPlayingArtworkHeight = 64;
constexpr size_t kNowPlayingArtworkPixelCount =
    kNowPlayingArtworkWidth * kNowPlayingArtworkHeight;

class NowPlayingSource {
 public:
  virtual ~NowPlayingSource() = default;
  virtual NowPlayingData Data() const = 0;
  virtual void SetArtworkEligible(bool eligible, uint32_t now_ms) = 0;
  // Copies only a complete artwork matching both values into `destination`.
  virtual bool CopyArtwork(uint64_t expected_identity, uint32_t expected_revision,
                           uint16_t *destination, size_t destination_count) const = 0;
};

}  // namespace pixoo::now_playing
