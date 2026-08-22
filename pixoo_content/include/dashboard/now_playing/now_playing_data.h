#pragma once

#include <cstddef>
#include <cstdint>

namespace pixoo::now_playing {

enum class PlaybackState : uint8_t {
  kUnknown, kPlaying, kPaused, kBuffering, kIdle, kOn, kOff, kStandby,
  kUnavailable,
};

enum class NowPlayingSourceState : uint8_t {
  kUnconfigured, kWaiting, kNoEntityData, kReady, kOffline, kStale,
};

enum class ArtworkAvailability : uint8_t { kNone, kPending, kReady, kFailed };

// UTF-8 bytes, never an owning C++ string. `size` excludes the terminating NUL.
struct BoundedText {
  static constexpr size_t kMaxBytes = 192;
  char bytes[kMaxBytes + 1]{};
  uint16_t size{0};
};

// Renderer-facing immutable snapshot. It is deliberately safe to copy across a
// task boundary without retaining framework-owned storage.
struct NowPlayingData {
  uint32_t config_revision{0};
  uint32_t publication_revision{0};
  uint32_t media_generation{0};
  uint32_t publication_time_ms{0};
  PlaybackState playback_state{PlaybackState::kUnknown};
  NowPlayingSourceState source_state{NowPlayingSourceState::kUnconfigured};
  BoundedText title{};
  BoundedText artist{};
  bool has_duration{false};
  bool has_position{false};
  uint32_t duration_ms{0};
  uint32_t position_ms{0};
  uint64_t media_identity{0};
  bool artwork_known{false};
  bool has_artwork_identity{false};
  uint64_t artwork_identity{0};
  ArtworkAvailability artwork_availability{ArtworkAvailability::kNone};
  uint32_t artwork_revision{0};
};

PlaybackState ParsePlaybackState(const char *value, size_t size);
bool IsActivePlaybackState(PlaybackState state);
bool IsInactivePlaybackState(PlaybackState state);

// Invalid (NaN/infinite/negative) values return false. Valid values saturate at
// UINT32_MAX milliseconds.
bool SecondsToMilliseconds(double seconds, uint32_t *milliseconds);

struct ProgressEstimate {
  bool visible{false};
  uint32_t position_ms{0};
  uint32_t duration_ms{0};
};

// Uses the snapshot publication time as the progress anchor. Unsigned
// subtraction intentionally makes elapsed time correct across one millis wrap.
ProgressEstimate EstimateProgress(const NowPlayingData &data, uint32_t now_ms);

// Stable FNV-1a byte hashing. Domains make unrelated identity classes unable to
// collide merely because their byte payloads are equal.
uint64_t HashNowPlayingBytes(uint64_t domain, const void *bytes, size_t size);
uint64_t ExplicitMediaIdentity(const char *content_id, size_t size);
uint64_t FallbackMediaIdentity(const BoundedText &title, const BoundedText &artist,
                               bool has_duration, uint32_t duration_ms,
                               bool has_artwork, uint64_t artwork_identity);

}  // namespace pixoo::now_playing
