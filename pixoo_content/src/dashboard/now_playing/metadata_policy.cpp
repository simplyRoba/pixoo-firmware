#include "metadata_policy.h"

#include <cstring>

#include "now_playing_text.h"

namespace pixoo::now_playing {
namespace {
bool SameId(const char *left, uint16_t left_size, const char *right, uint16_t right_size) {
  return left_size == right_size && std::memcmp(left, right, left_size) == 0;
}
}

void NowPlayingMetadataPolicy::Write_(NowPlayingData *published) const {
  if (published != nullptr) *published = data_;
}

void NowPlayingMetadataPolicy::Stage_(uint32_t now_ms) {
  if (!pending_) { pending_ = true; ++burst_; burst_started_ms_ = now_ms; }
  last_update_ms_ = now_ms;
}

bool NowPlayingMetadataPolicy::FieldInBurst_(bool seen, uint32_t burst) const {
  return seen && burst == burst_;
}

void NowPlayingMetadataPolicy::ClearStaging_() {
  pending_ = false; playback_seen_ = false; content_seen_ = false;
  title_.seen = false; artist_.seen = false; duration_.seen = false;
  position_.seen = false; artwork_.seen = false;
}

bool NowPlayingMetadataPolicy::Reset(bool configured, uint32_t config_revision,
                                     bool transport_connected, uint32_t now_ms,
                                     NowPlayingData *published) {
  configured_ = configured; connected_ = transport_connected; root_seen_ = false;
  attributes_allowed_ = true;
  pending_ = false; has_last_good_ = false; current_has_explicit_id_ = false;
  current_content_id_size_ = 0; ClearStaging_();
  data_ = {};
  data_.config_revision = config_revision;
  data_.publication_revision = 1;
  data_.publication_time_ms = now_ms;
  data_.source_state = !configured ? NowPlayingSourceState::kUnconfigured
                     : transport_connected ? NowPlayingSourceState::kWaiting
                                           : NowPlayingSourceState::kOffline;
  Write_(published);
  return true;
}

void NowPlayingMetadataPolicy::PublishOfflineOrStale_(uint32_t now_ms) {
  if (has_last_good_) {
    data_ = last_good_;
    data_.source_state = NowPlayingSourceState::kStale;
  } else {
    const uint32_t config = data_.config_revision;
    data_ = {};
    data_.config_revision = config;
    data_.source_state = NowPlayingSourceState::kOffline;
  }
  ++data_.publication_revision;
  data_.publication_time_ms = now_ms;
}

bool NowPlayingMetadataPolicy::SetTransportConnected(bool connected, uint32_t now_ms,
                                                      NowPlayingData *published) {
  if (connected_ == connected) return false;
  connected_ = connected;
  if (!configured_) return false;
  if (!connected) {
    ClearStaging_();
    root_seen_ = false;
    attributes_allowed_ = false;
    PublishOfflineOrStale_(now_ms); Write_(published); return true;
  }
  attributes_allowed_ = true;
  if (!has_last_good_) {
    data_.source_state = NowPlayingSourceState::kWaiting;
    data_.playback_state = PlaybackState::kUnknown;
    ++data_.publication_revision; data_.publication_time_ms = now_ms;
    Write_(published); return true;
  }
  // Retain the stale view until a new root state makes it ready again.
  return false;
}

bool NowPlayingMetadataPolicy::MarkNoEntityData(uint32_t now_ms,
                                                 NowPlayingData *published) {
  if (!configured_ || !connected_ || root_seen_) return false;
  data_.source_state = NowPlayingSourceState::kNoEntityData;
  data_.playback_state = PlaybackState::kUnknown;
  ++data_.publication_revision; data_.publication_time_ms = now_ms;
  Write_(published); return true;
}

bool NowPlayingMetadataPolicy::OnPlaybackState(PlaybackState state, uint32_t now_ms,
                                                NowPlayingData *published) {
  if (!configured_ || !connected_) return false;
  root_seen_ = true;
  if (IsInactivePlaybackState(state)) {
    attributes_allowed_ = false;
    ClearStaging_(); current_has_explicit_id_ = false; current_content_id_size_ = 0;
    const uint32_t config = data_.config_revision;
    data_ = {};
    data_.config_revision = config; data_.source_state = NowPlayingSourceState::kReady;
    data_.playback_state = state; data_.publication_time_ms = now_ms;
    ++data_.publication_revision; has_last_good_ = false;
    Write_(published); return true;
  }
  if (state == PlaybackState::kUnknown || state == PlaybackState::kUnavailable) {
    attributes_allowed_ = false;
    ClearStaging_(); PublishOfflineOrStale_(now_ms); Write_(published); return true;
  }
  attributes_allowed_ = true;
  Stage_(now_ms); staged_playback_ = state; playback_seen_ = true;
  playback_update_ms_ = now_ms; playback_burst_ = burst_;
  return false;
}

bool NowPlayingMetadataPolicy::OnContentId(const char *content_id, size_t size,
                                           uint32_t now_ms, NowPlayingData *) {
  if (size > kMaxContentIdBytes || (size != 0 && content_id == nullptr)) return false;
  if (!configured_ || !connected_ || !attributes_allowed_) return false;
  Stage_(now_ms); content_seen_ = true; content_burst_ = burst_;
  staged_has_explicit_id_ = size != 0; staged_content_id_size_ = static_cast<uint16_t>(size);
  if (size != 0) std::memcpy(staged_content_id_, content_id, size);
  staged_content_id_[size] = '\0';
  return false;
}

bool NowPlayingMetadataPolicy::OnTitle(const char *value, size_t size, uint32_t now_ms) {
  if (!configured_ || !connected_ || !attributes_allowed_) return false;
  Stage_(now_ms); SanitizeNowPlayingText(value, size, &title_.value);
  title_.seen = true; title_.update_ms = now_ms; title_.burst = burst_; return false;
}

bool NowPlayingMetadataPolicy::OnArtist(const char *value, size_t size, uint32_t now_ms) {
  if (!configured_ || !connected_ || !attributes_allowed_) return false;
  Stage_(now_ms); SanitizeNowPlayingText(value, size, &artist_.value);
  artist_.seen = true; artist_.update_ms = now_ms; artist_.burst = burst_; return false;
}

bool NowPlayingMetadataPolicy::OnDuration(bool present, double seconds, uint32_t now_ms) {
  uint32_t value = 0;
  if (present && !SecondsToMilliseconds(seconds, &value)) return false;
  if (!configured_ || !connected_ || !attributes_allowed_) return false;
  Stage_(now_ms); duration_.value = value; duration_.present = present;
  duration_.seen = true; duration_.update_ms = now_ms; duration_.burst = burst_; return false;
}

bool NowPlayingMetadataPolicy::OnPosition(bool present, double seconds, uint32_t now_ms) {
  uint32_t value = 0;
  if (present && !SecondsToMilliseconds(seconds, &value)) return false;
  if (!configured_ || !connected_ || !attributes_allowed_) return false;
  Stage_(now_ms); position_.value = value; position_.present = present;
  position_.seen = true; position_.update_ms = now_ms; position_.burst = burst_; return false;
}

bool NowPlayingMetadataPolicy::OnArtworkIdentity(bool present, uint64_t identity, uint32_t now_ms) {
  if (!configured_ || !connected_ || !attributes_allowed_) return false;
  Stage_(now_ms); artwork_.value = present ? identity : 0; artwork_.present = present;
  artwork_.seen = true; artwork_.update_ms = now_ms; artwork_.burst = burst_; return false;
}

bool NowPlayingMetadataPolicy::PublishIfDue(uint32_t now_ms, NowPlayingData *published) {
  if (!pending_ || !root_seen_ || !configured_ || !connected_) return false;
  if (now_ms - last_update_ms_ < kQuietWindowMs &&
      now_ms - burst_started_ms_ < kMaxBurstDelayMs) return false;
  return Publish_(now_ms, published);
}

bool NowPlayingMetadataPolicy::ForcePublish(uint32_t now_ms, NowPlayingData *published) {
  if (!pending_ || !root_seen_ || !configured_ || !connected_) return false;
  return Publish_(now_ms, published);
}

bool NowPlayingMetadataPolicy::Publish_(uint32_t now_ms, NowPlayingData *published) {
  NowPlayingData next = data_;
  const bool playback_in_burst =
      FieldInBurst_(playback_seen_, playback_burst_);
  const bool position_in_burst =
      FieldInBurst_(position_.seen, position_.burst);
  const bool title_in_burst = FieldInBurst_(title_.seen, title_.burst);
  const bool artist_in_burst = FieldInBurst_(artist_.seen, artist_.burst);
  const bool duration_in_burst = FieldInBurst_(duration_.seen, duration_.burst);
  const bool artwork_in_burst = FieldInBurst_(artwork_.seen, artwork_.burst);
  const bool id_seen = FieldInBurst_(content_seen_, content_burst_);
  const bool changed_explicit = id_seen &&
      (staged_has_explicit_id_ != current_has_explicit_id_ ||
       (staged_has_explicit_id_ && !SameId(staged_content_id_, staged_content_id_size_,
                                            current_content_id_, current_content_id_size_)));
  const BoundedText effective_title = title_in_burst ? title_.value : data_.title;
  const BoundedText effective_artist = artist_in_burst ? artist_.value : data_.artist;
  const bool effective_duration =
      duration_in_burst ? duration_.present : data_.has_duration;
  const uint32_t effective_duration_ms =
      duration_in_burst ? duration_.value : data_.duration_ms;
  const bool effective_art =
      artwork_in_burst ? artwork_.present : data_.has_artwork_identity;
  const uint64_t effective_art_id =
      artwork_in_burst ? artwork_.value : data_.artwork_identity;
  const bool effective_explicit =
      id_seen ? staged_has_explicit_id_ : current_has_explicit_id_;
  const uint64_t retained_identity = effective_explicit
      ? ExplicitMediaIdentity(id_seen ? staged_content_id_ : current_content_id_,
                              id_seen ? staged_content_id_size_ : current_content_id_size_)
      : FallbackMediaIdentity(effective_title, effective_artist, effective_duration,
                              effective_duration_ms, effective_art, effective_art_id);
  const bool new_generation =
      changed_explicit || (!effective_explicit && retained_identity != data_.media_identity);

  if (new_generation) {
    ++next.media_generation;
    next.title = title_in_burst ? title_.value : data_.title;
    next.artist = artist_in_burst ? artist_.value : data_.artist;
    next.has_duration = duration_in_burst && duration_.present;
    next.duration_ms = next.has_duration ? duration_.value : 0;
    next.has_artwork_identity = artwork_in_burst && artwork_.present;
    next.artwork_identity = next.has_artwork_identity ? artwork_.value : 0;
  } else {
    if (title_in_burst) next.title = title_.value;
    if (artist_in_burst) next.artist = artist_.value;
    if (duration_in_burst) {
      next.has_duration = duration_.present;
      next.duration_ms = duration_.present ? duration_.value : 0;
    }
    if (artwork_in_burst) {
      next.has_artwork_identity = artwork_.present;
      next.artwork_identity = artwork_.present ? artwork_.value : 0;
    }
  }

  // Replay the two callbacks that can change progress in receipt order. The
  // signed comparison is valid because all callbacks in a burst are within one
  // signed half-range of each other.
  PlaybackState timeline_playback = data_.playback_state;
  NowPlayingSourceState timeline_source = data_.source_state;
  bool timeline_has_position = new_generation ? false : data_.has_position;
  uint32_t timeline_position = new_generation ? 0 : data_.position_ms;
  uint32_t timeline_anchor_ms = data_.publication_time_ms;
  const auto advance_to = [&](uint32_t event_ms) {
    if (timeline_source == NowPlayingSourceState::kReady &&
        timeline_playback == PlaybackState::kPlaying && timeline_has_position &&
        next.has_duration) {
      const uint64_t advanced = static_cast<uint64_t>(timeline_position) +
                                (event_ms - timeline_anchor_ms);
      timeline_position = advanced > next.duration_ms
                              ? next.duration_ms
                              : static_cast<uint32_t>(advanced);
    }
    timeline_anchor_ms = event_ms;
  };
  const auto apply_position = [&]() {
    advance_to(position_.update_ms);
    timeline_has_position = position_.present;
    timeline_position = position_.present ? position_.value : 0;
  };
  const auto apply_playback = [&]() {
    advance_to(playback_update_ms_);
    timeline_playback = staged_playback_;
    timeline_source = NowPlayingSourceState::kReady;
  };
  if (position_in_burst && playback_in_burst) {
    if (static_cast<int32_t>(position_.update_ms - playback_update_ms_) < 0) {
      apply_position();
      apply_playback();
    } else {
      apply_playback();
      apply_position();
    }
  } else if (position_in_burst) {
    apply_position();
  } else if (playback_in_burst) {
    apply_playback();
  }
  advance_to(now_ms);

  next.has_position = timeline_has_position;
  next.position_ms = timeline_has_position ? timeline_position : 0;
  next.playback_state = timeline_playback;
  next.source_state = NowPlayingSourceState::kReady;
  if (next.has_duration && next.has_position &&
      next.position_ms > next.duration_ms)
    next.position_ms = next.duration_ms;
  if (!next.has_duration) next.duration_ms = 0;

  next.media_identity = effective_explicit
      ? ExplicitMediaIdentity(id_seen ? staged_content_id_ : current_content_id_,
                              id_seen ? staged_content_id_size_ : current_content_id_size_)
      : FallbackMediaIdentity(next.title, next.artist, next.has_duration,
                              next.duration_ms, next.has_artwork_identity,
                              next.artwork_identity);
  const bool art_changed =
      next.has_artwork_identity != data_.has_artwork_identity ||
      (next.has_artwork_identity &&
       next.artwork_identity != data_.artwork_identity);
  if (art_changed) {
    ++next.artwork_revision;
    next.artwork_availability = next.has_artwork_identity
                                    ? ArtworkAvailability::kPending
                                    : ArtworkAvailability::kNone;
  }
  next.publication_time_ms = now_ms;
  ++next.publication_revision;
  data_ = next;
  if (id_seen) {
    current_has_explicit_id_ = staged_has_explicit_id_;
    current_content_id_size_ = staged_content_id_size_;
    std::memcpy(current_content_id_, staged_content_id_,
                staged_content_id_size_ + 1);
  }
  if (IsActivePlaybackState(data_.playback_state)) {
    last_good_ = data_;
    has_last_good_ = true;
  }
  ClearStaging_();
  Write_(published);
  return true;
}

void NowPlayingMetadataPolicy::ReanchorProgress_(uint32_t now_ms) {
  const ProgressEstimate progress = EstimateProgress(data_, now_ms);
  if (progress.visible)
    data_.position_ms = progress.position_ms;
  data_.publication_time_ms = now_ms;
}

bool NowPlayingMetadataPolicy::BeginArtworkRetry(
    uint64_t identity, uint32_t revision, uint32_t now_ms,
    NowPlayingData *published) {
  if (!data_.has_artwork_identity || data_.artwork_identity != identity ||
      data_.artwork_revision != revision ||
      data_.artwork_availability != ArtworkAvailability::kFailed)
    return false;
  this->ReanchorProgress_(now_ms);
  data_.artwork_availability = ArtworkAvailability::kPending;
  ++data_.publication_revision;
  if (IsActivePlaybackState(data_.playback_state) &&
      data_.source_state == NowPlayingSourceState::kReady) {
    last_good_ = data_;
    has_last_good_ = true;
  }
  Write_(published);
  return true;
}

bool NowPlayingMetadataPolicy::CompleteArtwork(uint64_t identity, uint32_t revision,
                                               bool succeeded, uint32_t now_ms,
                                               NowPlayingData *published) {
  if (!data_.has_artwork_identity || data_.artwork_identity != identity ||
      data_.artwork_revision != revision || data_.artwork_availability != ArtworkAvailability::kPending)
    return false;
  this->ReanchorProgress_(now_ms);
  data_.artwork_availability = succeeded ? ArtworkAvailability::kReady : ArtworkAvailability::kFailed;
  ++data_.publication_revision;
  if (IsActivePlaybackState(data_.playback_state) && data_.source_state == NowPlayingSourceState::kReady) { last_good_ = data_; has_last_good_ = true; }
  Write_(published); return true;
}

}  // namespace pixoo::now_playing
