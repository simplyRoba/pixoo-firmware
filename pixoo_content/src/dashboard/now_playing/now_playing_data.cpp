#include "now_playing_data.h"

#include <cmath>
#include <cstring>
#include <limits>

namespace pixoo::now_playing {
namespace {
constexpr uint64_t kFnvOffset = 14695981039346656037ull;
constexpr uint64_t kFnvPrime = 1099511628211ull;
constexpr uint64_t kExplicitDomain = 0x4558504c49434954ull;  // EXPLICIT
constexpr uint64_t kFallbackDomain = 0x46414c4c4241434bull;  // FALLBACK

void HashU32(uint64_t *hash, uint32_t value) {
  for (int i = 0; i < 4; ++i) {
    *hash ^= static_cast<uint8_t>(value >> (i * 8));
    *hash *= kFnvPrime;
  }
}

void HashU64(uint64_t *hash, uint64_t value) {
  for (int i = 0; i < 8; ++i) {
    *hash ^= static_cast<uint8_t>(value >> (i * 8));
    *hash *= kFnvPrime;
  }
}
}  // namespace

PlaybackState ParsePlaybackState(const char *value, size_t size) {
  if (value == nullptr) return PlaybackState::kUnknown;
#define MATCH(text, state) \
  if (size == sizeof(text) - 1 && std::memcmp(value, text, sizeof(text) - 1) == 0) return state
  MATCH("playing", PlaybackState::kPlaying);
  MATCH("paused", PlaybackState::kPaused);
  MATCH("buffering", PlaybackState::kBuffering);
  MATCH("idle", PlaybackState::kIdle);
  MATCH("on", PlaybackState::kOn);
  MATCH("off", PlaybackState::kOff);
  MATCH("standby", PlaybackState::kStandby);
  MATCH("unavailable", PlaybackState::kUnavailable);
  MATCH("unknown", PlaybackState::kUnknown);
#undef MATCH
  return PlaybackState::kUnknown;
}

bool IsActivePlaybackState(PlaybackState state) {
  return state == PlaybackState::kPlaying || state == PlaybackState::kPaused ||
         state == PlaybackState::kBuffering;
}

bool IsInactivePlaybackState(PlaybackState state) {
  return state == PlaybackState::kIdle || state == PlaybackState::kOn ||
         state == PlaybackState::kOff || state == PlaybackState::kStandby;
}

bool SecondsToMilliseconds(double seconds, uint32_t *milliseconds) {
  if (!std::isfinite(seconds) || seconds < 0.0) return false;
  const double maximum = static_cast<double>(std::numeric_limits<uint32_t>::max());
  const double value = seconds >= maximum / 1000.0 ? maximum : seconds * 1000.0;
  if (milliseconds != nullptr) *milliseconds = static_cast<uint32_t>(value);
  return true;
}

ProgressEstimate EstimateProgress(const NowPlayingData &data, uint32_t now_ms) {
  ProgressEstimate result{};
  if (!data.has_duration || !data.has_position) return result;
  result.visible = true;
  result.duration_ms = data.duration_ms;
  result.position_ms = data.position_ms > data.duration_ms ? data.duration_ms : data.position_ms;
  if (data.source_state == NowPlayingSourceState::kReady &&
      data.playback_state == PlaybackState::kPlaying) {
    const uint32_t elapsed = now_ms - data.publication_time_ms;
    const uint64_t advanced = static_cast<uint64_t>(result.position_ms) + elapsed;
    result.position_ms = advanced > result.duration_ms ? result.duration_ms
                                                        : static_cast<uint32_t>(advanced);
  }
  return result;
}

uint64_t HashNowPlayingBytes(uint64_t domain, const void *bytes, size_t size) {
  uint64_t hash = kFnvOffset;
  HashU64(&hash, domain);
  const auto *input = static_cast<const uint8_t *>(bytes);
  for (size_t i = 0; i < size; ++i) {
    hash ^= input[i];
    hash *= kFnvPrime;
  }
  HashU64(&hash, static_cast<uint64_t>(size));
  return hash;
}

uint64_t ExplicitMediaIdentity(const char *content_id, size_t size) {
  return HashNowPlayingBytes(kExplicitDomain, content_id, size);
}

uint64_t FallbackMediaIdentity(const BoundedText &title, const BoundedText &artist,
                               bool has_duration, uint32_t duration_ms,
                               bool has_artwork, uint64_t artwork_identity) {
  uint64_t hash = kFnvOffset;
  HashU64(&hash, kFallbackDomain);
  HashU32(&hash, title.size);
  for (uint16_t i = 0; i < title.size; ++i) { hash ^= static_cast<uint8_t>(title.bytes[i]); hash *= kFnvPrime; }
  HashU32(&hash, artist.size);
  for (uint16_t i = 0; i < artist.size; ++i) { hash ^= static_cast<uint8_t>(artist.bytes[i]); hash *= kFnvPrime; }
  HashU32(&hash, has_duration ? 1u : 0u);
  HashU32(&hash, duration_ms);
  HashU32(&hash, has_artwork ? 1u : 0u);
  HashU64(&hash, artwork_identity);
  return hash;
}

}  // namespace pixoo::now_playing
