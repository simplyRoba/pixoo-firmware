#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "now_playing_source.h"

namespace esphome::pixoo64_render_test {

class StaticNowPlayingSource final
    : public pixoo::now_playing::NowPlayingSource {
 public:
  static constexpr size_t kMaxSnapshots = 32;

  void add_snapshot(
      uint32_t at_ms, pixoo::now_playing::NowPlayingSourceState source_state,
      pixoo::now_playing::PlaybackState playback_state,
      const std::string &title, const std::string &artist,
      bool has_duration, uint32_t duration_ms, bool has_position,
      uint32_t position_ms, uint64_t media_identity,
      bool has_artwork_identity, uint64_t artwork_identity,
      pixoo::now_playing::ArtworkAvailability artwork_availability,
      uint32_t artwork_revision, uint32_t artwork_copy_ready_at_ms);

  static void SetCurrentRenderTime(uint32_t now_ms) {
    current_render_time_ms_ = now_ms;
  }

  pixoo::now_playing::NowPlayingData Data() const override;
  void SetArtworkEligible(bool eligible, uint32_t now_ms) override;
  bool CopyArtwork(uint64_t expected_identity, uint32_t expected_revision,
                   uint16_t *destination,
                   size_t destination_count) const override;

  uint32_t eligible_true_count() const { return this->eligible_true_count_; }
  uint32_t eligible_false_count() const { return this->eligible_false_count_; }
  uint32_t last_eligibility_ms() const { return this->last_eligibility_ms_; }
  uint32_t data_count() const { return this->data_count_; }
  uint32_t copy_count() const { return this->copy_count_; }

 private:
  struct TimedSnapshot {
    uint32_t at_ms{0};
    uint32_t artwork_copy_ready_at_ms{0};
    pixoo::now_playing::NowPlayingData data{};
  };

  const TimedSnapshot &Current_() const;
  static uint16_t ArtworkPixel_(uint64_t identity, uint32_t revision, int x,
                                int y);

  static uint32_t current_render_time_ms_;
  TimedSnapshot snapshots_[kMaxSnapshots]{};
  size_t snapshot_count_{0};
  mutable uint32_t data_count_{0};
  mutable uint32_t copy_count_{0};
  uint32_t eligible_true_count_{0};
  uint32_t eligible_false_count_{0};
  uint32_t last_eligibility_ms_{0};
  bool eligible_{false};
};

}  // namespace esphome::pixoo64_render_test
