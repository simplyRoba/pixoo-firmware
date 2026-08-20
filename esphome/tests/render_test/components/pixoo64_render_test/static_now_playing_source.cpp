#include "static_now_playing_source.h"

#include "now_playing_art.h"
#include "now_playing_text.h"

namespace esphome::pixoo64_render_test {

uint32_t StaticNowPlayingSource::current_render_time_ms_ = 0;

void StaticNowPlayingSource::add_snapshot(
    uint32_t at_ms,
    pixoo::now_playing::NowPlayingSourceState source_state,
    pixoo::now_playing::PlaybackState playback_state,
    const std::string &title, const std::string &artist, bool has_duration,
    uint32_t duration_ms, bool has_position, uint32_t position_ms,
    uint64_t media_identity, bool has_artwork_identity,
    uint64_t artwork_identity,
    pixoo::now_playing::ArtworkAvailability artwork_availability,
    uint32_t artwork_revision, uint32_t artwork_copy_ready_at_ms,
    bool has_artwork_content_identity, uint64_t artwork_content_identity,
    uint32_t artwork_content_revision) {
  if (this->snapshot_count_ >= kMaxSnapshots)
    return;
  TimedSnapshot &entry = this->snapshots_[this->snapshot_count_];
  entry.at_ms = at_ms;
  entry.artwork_copy_ready_at_ms = artwork_copy_ready_at_ms;
  entry.has_artwork_content_identity = has_artwork_content_identity;
  entry.artwork_content_identity = artwork_content_identity;
  entry.artwork_content_revision = artwork_content_revision;
  entry.data.config_revision = 1;
  entry.data.publication_revision =
      static_cast<uint32_t>(this->snapshot_count_ + 1);
  entry.data.media_generation =
      static_cast<uint32_t>(this->snapshot_count_ + 1);
  entry.data.publication_time_ms = at_ms;
  entry.data.source_state = source_state;
  entry.data.playback_state = playback_state;
  pixoo::now_playing::SanitizeNowPlayingText(
      title.data(), title.size(), &entry.data.title);
  pixoo::now_playing::SanitizeNowPlayingText(
      artist.data(), artist.size(), &entry.data.artist);
  entry.data.has_duration = has_duration;
  entry.data.duration_ms = has_duration ? duration_ms : 0;
  entry.data.has_position = has_position;
  entry.data.position_ms = has_position ? position_ms : 0;
  entry.data.media_identity = media_identity;
  entry.data.has_artwork_identity = has_artwork_identity;
  entry.data.artwork_identity = has_artwork_identity ? artwork_identity : 0;
  entry.data.artwork_availability =
      has_artwork_identity ? artwork_availability
                           : pixoo::now_playing::ArtworkAvailability::kNone;
  entry.data.artwork_revision =
      has_artwork_identity ? artwork_revision : 0;
  ++this->snapshot_count_;
}

const StaticNowPlayingSource::TimedSnapshot &StaticNowPlayingSource::Current_()
    const {
  static const TimedSnapshot empty{};
  if (this->snapshot_count_ == 0)
    return empty;
  size_t selected = 0;
  for (size_t i = 1; i < this->snapshot_count_; ++i) {
    if (static_cast<int32_t>(current_render_time_ms_ -
                             this->snapshots_[i].at_ms) < 0)
      break;
    selected = i;
  }
  return this->snapshots_[selected];
}

pixoo::now_playing::NowPlayingData StaticNowPlayingSource::Data() const {
  ++this->data_count_;
  return this->Current_().data;
}

void StaticNowPlayingSource::SetArtworkEligible(bool eligible,
                                                 uint32_t now_ms) {
  this->eligible_ = eligible;
  this->last_eligibility_ms_ = now_ms;
  if (eligible)
    ++this->eligible_true_count_;
  else
    ++this->eligible_false_count_;
}

uint16_t StaticNowPlayingSource::ArtworkPixel_(uint64_t identity,
                                               uint32_t revision, int x,
                                               int y) {
  const uint8_t phase = static_cast<uint8_t>(identity ^ (identity >> 17) ^
                                             (revision * 29u));
  uint8_t red = static_cast<uint8_t>(35 + ((x * 4 + phase) % 170));
  uint8_t green = static_cast<uint8_t>(28 + ((y * 5 + phase * 3) % 180));
  uint8_t blue = static_cast<uint8_t>(55 + (((x + y) * 3 + phase) % 175));
  const int dx = x - 31;
  const int dy = y - 27;
  if (dx * dx + dy * dy < 14 * 14) {
    red = static_cast<uint8_t>(210 + ((x + phase) & 31));
    green = static_cast<uint8_t>(70 + ((y * 3 + phase) & 63));
    blue = static_cast<uint8_t>(105 + ((x + y + phase) & 63));
  }
  if (((x / 8) + (y / 8)) % 2 == 0) {
    red = static_cast<uint8_t>(red * 4 / 5);
    green = static_cast<uint8_t>(green * 4 / 5);
    blue = static_cast<uint8_t>(blue * 4 / 5);
  }
  if ((x + y + phase) % 19 == 0) {
    red = 246;
    green = 231;
    blue = 130;
  }
  return pixoo::now_playing::Rgb565(red, green, blue);
}

bool StaticNowPlayingSource::CopyArtwork(
    uint64_t expected_identity, uint32_t expected_revision,
    uint16_t *destination, size_t destination_count) const {
  ++this->copy_count_;
  const TimedSnapshot &entry = this->Current_();
  if (!this->eligible_ || destination == nullptr ||
      destination_count < pixoo::now_playing::kArtworkPixelCount ||
      entry.data.artwork_availability !=
          pixoo::now_playing::ArtworkAvailability::kReady ||
      !entry.data.has_artwork_identity ||
      entry.data.artwork_identity != expected_identity ||
      entry.data.artwork_revision != expected_revision ||
      static_cast<int32_t>(current_render_time_ms_ -
                           entry.artwork_copy_ready_at_ms) < 0)
    return false;
  for (int y = 0; y < 64; ++y) {
    for (int x = 0; x < 64; ++x) {
      destination[static_cast<size_t>(y) * 64u + x] = ArtworkPixel_(
          entry.has_artwork_content_identity ? entry.artwork_content_identity
                                             : expected_identity,
          entry.has_artwork_content_identity ? entry.artwork_content_revision
                                             : expected_revision,
          x, y);
    }
  }
  return true;
}

}  // namespace esphome::pixoo64_render_test
