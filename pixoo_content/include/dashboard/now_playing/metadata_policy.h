#pragma once

#include <cstddef>
#include <cstdint>

#include "now_playing_data.h"

namespace pixoo::now_playing {

// Main-task metadata coalescer. All callbacks copy their inputs synchronously;
// it owns no framework objects or heap allocations. Methods returning true have
// published immediately and copy that exact snapshot to `published` when it is
// non-null. All other callback methods return false and callers obtain delayed
// snapshots from PublishIfDue/ForcePublish using the same convention.
class NowPlayingMetadataPolicy {
 public:
  static constexpr uint32_t kQuietWindowMs = 120;
  static constexpr uint32_t kMaxBurstDelayMs = 500;
  static constexpr size_t kMaxContentIdBytes = 256;

  bool Reset(bool configured, uint32_t config_revision, bool transport_connected,
             uint32_t now_ms, NowPlayingData *published = nullptr);
  bool SetTransportConnected(bool connected, uint32_t now_ms,
                             NowPlayingData *published = nullptr);
  bool MarkNoEntityData(uint32_t now_ms, NowPlayingData *published = nullptr);

  bool OnPlaybackState(PlaybackState state, uint32_t now_ms,
                       NowPlayingData *published = nullptr);
  // Empty content ID means no explicit identity. An ID over kMaxContentIdBytes
  // is rejected and leaves all policy state unchanged.
  bool OnContentId(const char *content_id, size_t size, uint32_t now_ms,
                   NowPlayingData *published = nullptr);
  bool OnTitle(const char *value, size_t size, uint32_t now_ms);
  bool OnArtist(const char *value, size_t size, uint32_t now_ms);
  bool OnDuration(bool present, double seconds, uint32_t now_ms);
  bool OnPosition(bool present, double seconds, uint32_t now_ms);
  bool OnArtworkIdentity(bool present, uint64_t identity, uint32_t now_ms);

  bool PublishIfDue(uint32_t now_ms, NowPlayingData *published = nullptr);
  bool ForcePublish(uint32_t now_ms, NowPlayingData *published = nullptr);
  bool BeginArtworkRetry(uint64_t identity, uint32_t revision,
                         uint32_t now_ms,
                         NowPlayingData *published = nullptr);
  bool CompleteArtwork(uint64_t identity, uint32_t revision, bool succeeded,
                       uint32_t now_ms, NowPlayingData *published = nullptr);
  NowPlayingData Data() const { return data_; }

 private:
  struct TextField { BoundedText value{}; bool seen{false}; uint32_t update_ms{0}; uint32_t burst{0}; };
  struct MillisField { uint32_t value{0}; bool present{false}; bool seen{false}; uint32_t update_ms{0}; uint32_t burst{0}; };
  struct ArtworkField { uint64_t value{0}; bool present{false}; bool seen{false}; uint32_t update_ms{0}; uint32_t burst{0}; };

  void Stage_(uint32_t now_ms);
  void ClearStaging_();
  bool Publish_(uint32_t now_ms, NowPlayingData *published);
  void ReanchorProgress_(uint32_t now_ms);
  void PublishOfflineOrStale_(uint32_t now_ms);
  bool FieldInBurst_(bool seen, uint32_t burst) const;
  void Write_(NowPlayingData *published) const;

  NowPlayingData data_{};
  NowPlayingData last_good_{};
  bool has_last_good_{false};
  bool configured_{false};
  bool connected_{false};
  bool root_seen_{false};
  bool attributes_allowed_{true};
  bool pending_{false};
  uint32_t burst_{0};
  uint32_t burst_started_ms_{0};
  uint32_t last_update_ms_{0};
  PlaybackState staged_playback_{PlaybackState::kUnknown};
  bool playback_seen_{false};
  uint32_t playback_update_ms_{0};
  uint32_t playback_burst_{0};
  char staged_content_id_[kMaxContentIdBytes + 1]{};
  uint16_t staged_content_id_size_{0};
  bool staged_has_explicit_id_{false};
  bool content_seen_{false};
  uint32_t content_burst_{0};
  char current_content_id_[kMaxContentIdBytes + 1]{};
  uint16_t current_content_id_size_{0};
  bool current_has_explicit_id_{false};
  TextField title_{};
  TextField artist_{};
  MillisField duration_{};
  MillisField position_{};
  ArtworkField artwork_{};
};

}  // namespace pixoo::now_playing
