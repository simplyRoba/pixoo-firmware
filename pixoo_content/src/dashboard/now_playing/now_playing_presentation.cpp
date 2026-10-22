#include "now_playing_presentation.h"

#include <cstring>

namespace pixoo::now_playing {
namespace {

constexpr uint64_t kVisualArtworkDomain = 0x56495355414c4152ull;
constexpr uint64_t kVisualMediaDomain = 0x56495355414c4d45ull;
constexpr uint64_t kVisualStateDomain = 0x56495355414c5354ull;
constexpr uint64_t kTitleDomain = 0x4d4152515449544cull;
constexpr uint64_t kArtistDomain = 0x4d41525141525449ull;

uint64_t DomainKey(uint64_t domain, uint64_t value) {
  return pixoo::now_playing::HashNowPlayingBytes(domain, &value,
                                                  sizeof(value));
}

bool TextChanged(const NowPlayingData &left, const NowPlayingData &right) {
  return left.title.size != right.title.size ||
         std::memcmp(left.title.bytes, right.title.bytes, left.title.size) != 0 ||
         left.artist.size != right.artist.size ||
         std::memcmp(left.artist.bytes, right.artist.bytes,
                     left.artist.size) != 0;
}

uint8_t MixChannel(uint8_t from, uint8_t to, uint8_t amount) {
  return static_cast<uint8_t>(
      (static_cast<uint32_t>(from) * (255u - amount) +
       static_cast<uint32_t>(to) * amount + 127u) /
      255u);
}

}  // namespace

bool NowPlayingPresentation::ShowsMetadata(const NowPlayingData &data) {
  return (data.source_state == NowPlayingSourceState::kReady ||
          data.source_state == NowPlayingSourceState::kStale) &&
         pixoo::now_playing::IsActivePlaybackState(data.playback_state);
}

void NowPlayingPresentation::OnShow() {
  if (this->hidden_)
    this->ResetPresentation_();
  this->hidden_ = false;
}

void NowPlayingPresentation::SetAverage_(int8_t slot, ArtworkAverage average) {
  auto &info = this->buffer_info_[slot];
  info.average_red = average.red;
  info.average_green = average.green;
  info.average_blue = average.blue;
}

bool NowPlayingPresentation::ReadyToShow(const NowPlayingData &data, bool settled) {
  if (!settled)
    return false;
  if (data.source_state == NowPlayingSourceState::kUnconfigured)
    return true;
  if (data.source_state != NowPlayingSourceState::kReady ||
      (ShowsMetadata(data) && !data.artwork_known) ||
      (data.has_artwork_identity &&
       data.artwork_availability != ArtworkAvailability::kReady &&
       data.artwork_availability != ArtworkAvailability::kFailed))
    return false;
  return true;
}

void NowPlayingPresentation::ResetPresentation_() {
  this->snapshot_ = {};
  this->displayed_ = {};
  this->transition_from_data_ = {};
  this->transition_data_ = {};
  this->title_marquee_ = {};
  this->artist_marquee_ = {};
  this->visual_timeline_ = {};
  this->dim_timeline_ = {};
  this->buffer_info_[0] = {};
  this->buffer_info_[1] = {};
  this->title_width_ = 0;
  this->artist_width_ = 0;
  this->title_offset_ = 0;
  this->artist_offset_ = 0;
  this->front_buffer_ = 0;
  this->transition_buffer_ = 1;
  this->transition_kind_ = TransitionKind::kNone;
  this->crossfade_ = 0;
  this->text_opacity_ = 255;
  this->dim_from_ = 255;
  this->dim_value_ = 255;
  this->dim_target_ = 255;
  this->initialized_ = false;
  this->dim_transitioning_ = false;
  this->text_transitioning_ = false;
  this->text_data_switched_ = false;
}

uint64_t NowPlayingPresentation::VisualKey_(const NowPlayingData &data) {
  if ((data.source_state == NowPlayingSourceState::kReady ||
       data.source_state == NowPlayingSourceState::kStale) &&
      pixoo::now_playing::IsActivePlaybackState(data.playback_state)) {
    if (data.has_artwork_identity)
      return DomainKey(kVisualArtworkDomain, data.artwork_identity);
    if (data.media_identity != 0)
      return DomainKey(kVisualMediaDomain, data.media_identity);
  }
  const uint64_t state =
      (static_cast<uint64_t>(data.source_state) << 32) |
      static_cast<uint8_t>(data.playback_state);
  return DomainKey(kVisualStateDomain, state);
}

uint8_t NowPlayingPresentation::DesiredDim_(const NowPlayingData &data) {
  if (data.source_state == NowPlayingSourceState::kStale)
    return 112;
  if (data.playback_state == PlaybackState::kPaused)
    return 150;
  return 255;
}

void NowPlayingPresentation::InitializeVisual_(uint64_t visual_key,
                                            NowPlayingPresentationArtwork &artwork) {
  this->front_buffer_ = 0;
  this->transition_buffer_ = 1;
  const ArtworkAverage placeholder_average = artwork.PreparePlaceholder(this->front_buffer_, visual_key);
  this->buffer_info_[this->front_buffer_] = {
      visual_key, 0, 0, BufferKind::kPlaceholder, 0, 0, 0};
  this->SetAverage_(this->front_buffer_, placeholder_average);
  this->displayed_ = this->snapshot_;
  this->transition_from_data_ = this->snapshot_;
  this->transition_data_ = this->snapshot_;
  this->initialized_ = true;

  if (!this->snapshot_.has_artwork_identity ||
      this->snapshot_.artwork_availability != ArtworkAvailability::kReady)
    return;
  ArtworkAverage average;
  if (artwork.CopyArtwork(this->front_buffer_, this->snapshot_.artwork_identity,
                          this->snapshot_.artwork_revision, average)) {
    this->buffer_info_[this->front_buffer_] = {
        visual_key, this->snapshot_.artwork_identity,
        this->snapshot_.artwork_revision, BufferKind::kArtwork, 0, 0, 0};
    this->SetAverage_(this->front_buffer_, average);
  }
}

void NowPlayingPresentation::StageMetadata_(const NowPlayingData &data) {
  this->transition_from_data_ = this->displayed_;
  this->transition_data_ = data;
  this->text_data_switched_ = false;
  this->text_transitioning_ =
      TextChanged(this->displayed_, data) &&
      (ShowsMetadata(this->displayed_) || ShowsMetadata(data));
  this->text_opacity_ = 255;
  if (!this->text_transitioning_)
    this->displayed_ = data;
}

void NowPlayingPresentation::StartIdentityTransition_(
    const NowPlayingData &data, uint64_t visual_key, uint32_t now_ms,
    NowPlayingPresentationArtwork &artwork) {
  this->transition_buffer_ = static_cast<int8_t>(1 - this->front_buffer_);
  const ArtworkAverage placeholder_average = artwork.PreparePlaceholder(this->transition_buffer_, visual_key);
  this->buffer_info_[this->transition_buffer_] = {
      visual_key, 0, 0, BufferKind::kPlaceholder, 0, 0, 0};
  this->SetAverage_(this->transition_buffer_, placeholder_average);
  if (artwork.EqualArtwork(this->front_buffer_, this->transition_buffer_)) {
    this->buffer_info_[this->front_buffer_] =
        this->buffer_info_[this->transition_buffer_];
    this->StartTextTransition_(data, now_ms);
    return;
  }
  this->StageMetadata_(data);
  this->transition_kind_ = TransitionKind::kIdentityPlaceholder;
  this->crossfade_ = 0;
  this->visual_timeline_.Start(now_ms, kIdentityTransitionMs);
}

bool NowPlayingPresentation::StartArtworkTransition_(
    const NowPlayingData &data, uint64_t visual_key, uint32_t now_ms,
    NowPlayingPresentationArtwork &artwork) {
  if (!data.has_artwork_identity ||
      data.artwork_availability != ArtworkAvailability::kReady)
    return false;
  this->transition_buffer_ = static_cast<int8_t>(1 - this->front_buffer_);
  ArtworkAverage average;
  if (!artwork.CopyArtwork(this->transition_buffer_, data.artwork_identity,
                           data.artwork_revision, average))
    return false;
  this->buffer_info_[this->transition_buffer_] = {
      visual_key, data.artwork_identity, data.artwork_revision,
      BufferKind::kArtwork, 0, 0, 0};
  this->SetAverage_(this->transition_buffer_, average);
  if (artwork.EqualArtwork(this->front_buffer_, this->transition_buffer_)) {
    this->buffer_info_[this->front_buffer_] =
        this->buffer_info_[this->transition_buffer_];
    this->StartTextTransition_(data, now_ms);
    return true;
  }
  this->StageMetadata_(data);
  this->transition_kind_ = TransitionKind::kArtwork;
  this->crossfade_ = 0;
  this->visual_timeline_.Start(now_ms, kArtworkTransitionMs);
  return true;
}

void NowPlayingPresentation::StartTextTransition_(const NowPlayingData &data,
                                               uint32_t now_ms) {
  this->StageMetadata_(data);
  if (!this->text_transitioning_)
    return;
  this->transition_kind_ = TransitionKind::kText;
  this->crossfade_ = 0;
  this->visual_timeline_.Start(now_ms, kArtworkTransitionMs);
}

bool NowPlayingPresentation::TransitionMatches_(const NowPlayingData &data) const {
  return VisualKey_(data) == VisualKey_(this->transition_data_) &&
         data.media_identity == this->transition_data_.media_identity &&
         data.has_artwork_identity == this->transition_data_.has_artwork_identity &&
         data.artwork_identity == this->transition_data_.artwork_identity &&
         data.artwork_availability == this->transition_data_.artwork_availability &&
         data.artwork_revision == this->transition_data_.artwork_revision &&
         !TextChanged(data, this->transition_data_);
}

void NowPlayingPresentation::CancelTransition_() {
  this->displayed_ = this->transition_from_data_;
  this->transition_kind_ = TransitionKind::kNone;
  this->crossfade_ = 0;
  this->text_transitioning_ = false;
  this->text_data_switched_ = false;
  this->text_opacity_ = 255;
}

void NowPlayingPresentation::AdvanceTransition_(uint32_t now_ms) {
  if (this->transition_kind_ == TransitionKind::kNone)
    return;
  const uint8_t linear = this->visual_timeline_.Linear(now_ms);
  if (this->text_transitioning_) {
    if (!this->text_data_switched_ && linear >= 128) {
      this->displayed_ = this->transition_data_;
      this->text_data_switched_ = true;
    }
    if (linear < 128) {
      this->text_opacity_ = static_cast<uint8_t>(
          255u - (static_cast<uint16_t>(linear) * 255u) / 127u);
    } else {
      this->text_opacity_ = static_cast<uint8_t>(
          (static_cast<uint16_t>(linear - 128u) * 255u) / 127u);
    }
  }
  if (!this->visual_timeline_.Complete(now_ms)) {
    this->crossfade_ = this->visual_timeline_.Smooth(now_ms);
    return;
  }

  if (this->transition_kind_ != TransitionKind::kText) {
    this->front_buffer_ = this->transition_buffer_;
    this->transition_buffer_ = static_cast<int8_t>(1 - this->front_buffer_);
  }
  this->displayed_ = this->transition_data_;
  this->transition_kind_ = TransitionKind::kNone;
  this->crossfade_ = 0;
  this->text_transitioning_ = false;
  this->text_opacity_ = 255;
}

void NowPlayingPresentation::AdvanceDim_(uint32_t now_ms) {
  if (this->dim_transitioning_) {
    const uint8_t amount = this->dim_timeline_.Smooth(now_ms);
    this->dim_value_ = MixChannel(this->dim_from_, this->dim_target_, amount);
    if (this->dim_timeline_.Complete(now_ms)) {
      this->dim_value_ = this->dim_target_;
      this->dim_transitioning_ = false;
    }
  }
  const uint8_t desired = DesiredDim_(this->displayed_);
  if (desired == this->dim_target_)
    return;
  this->dim_from_ = this->dim_value_;
  this->dim_target_ = desired;
  this->dim_timeline_.Start(now_ms, kDimTransitionMs);
  this->dim_transitioning_ = true;
}

void NowPlayingPresentation::UpdateTextLayout(int title_width, int artist_width, uint32_t now_ms) {
  if (!ShowsMetadata(this->displayed_)) {
    this->title_width_ = 0;
    this->artist_width_ = 0;
    this->title_offset_ = 0;
    this->artist_offset_ = 0;
    return;
  }
  const char *title = this->displayed_.title.size == 0
                          ? "NOW PLAYING"
                          : this->displayed_.title.bytes;
  const char *artist = this->displayed_.artist.bytes;
  this->title_width_ = title_width;
  this->artist_width_ = artist_width;
  const uint64_t title_text_identity = pixoo::now_playing::HashNowPlayingBytes(
      kTitleDomain, title, this->displayed_.title.size == 0
                               ? sizeof("NOW PLAYING") - 1
                               : this->displayed_.title.size);
  const uint64_t artist_text_identity = pixoo::now_playing::HashNowPlayingBytes(
      kArtistDomain, artist, this->displayed_.artist.size);
  const uint64_t title_identity = pixoo::now_playing::HashNowPlayingBytes(
      title_text_identity, &this->displayed_.media_identity,
      sizeof(this->displayed_.media_identity));
  const uint64_t artist_identity = pixoo::now_playing::HashNowPlayingBytes(
      artist_text_identity, &this->displayed_.media_identity,
      sizeof(this->displayed_.media_identity));
  this->title_offset_ = this->title_marquee_.Offset(
      title_identity, this->title_width_, kTextWidth, now_ms, kMarqueePauseMs,
      kMarqueeStepMs, kMarqueeGapPx);
  this->artist_offset_ = this->artist_marquee_.Offset(
      artist_identity, this->artist_width_, kTextWidth, now_ms, kMarqueePauseMs,
      kMarqueeStepMs, kMarqueeGapPx);
}

void NowPlayingPresentation::Update(const NowPlayingData &snapshot, uint32_t now_ms,
                                    NowPlayingPresentationArtwork &artwork) {
  this->snapshot_ = snapshot;
  const uint64_t desired_key = VisualKey_(this->snapshot_);
  if (!this->initialized_) {
    this->InitializeVisual_(desired_key, artwork);
  } else {
    // Never finish a staged presentation against a newer source sample. A
    // pending replacement deliberately restores the retained presentation;
    // a failed, idle, or newer ready sample is then handled below as its own
    // transition from that same coherent front buffer.
    if (this->transition_kind_ != TransitionKind::kNone &&
        !this->TransitionMatches_(this->snapshot_)) {
      this->CancelTransition_();
    } else if (this->transition_kind_ != TransitionKind::kNone) {
      this->transition_data_ = this->snapshot_;
    }
    this->AdvanceTransition_(now_ms);
    if (this->transition_kind_ == TransitionKind::kNone) {
      const BufferInfo &front = this->buffer_info_[this->front_buffer_];
      const bool pending_artwork =
          ShowsMetadata(this->displayed_) && this->snapshot_.has_artwork_identity &&
          this->snapshot_.artwork_availability == ArtworkAvailability::kPending;
      if (pending_artwork) {
        // A pending replacement is not a presentation update: keep the old
        // metadata and pixels together until a ready or failed result arrives.
      } else if (front.visual_key != desired_key) {
        if (this->snapshot_.has_artwork_identity &&
            this->snapshot_.artwork_availability == ArtworkAvailability::kReady) {
          if (!this->StartArtworkTransition_(this->snapshot_, desired_key, now_ms, artwork))
            this->StartIdentityTransition_(this->snapshot_, desired_key, now_ms, artwork);
        } else {
          this->StartIdentityTransition_(this->snapshot_, desired_key, now_ms, artwork);
        }
      } else if (this->snapshot_.has_artwork_identity &&
                 this->snapshot_.artwork_availability == ArtworkAvailability::kReady &&
                 !(front.kind == BufferKind::kArtwork &&
                   front.artwork_identity == this->snapshot_.artwork_identity &&
                   front.artwork_revision == this->snapshot_.artwork_revision)) {
        if (!this->StartArtworkTransition_(this->snapshot_, desired_key, now_ms, artwork))
          this->StartIdentityTransition_(this->snapshot_, desired_key, now_ms, artwork);
      } else if (front.kind == BufferKind::kArtwork &&
                 this->snapshot_.has_artwork_identity &&
                 this->snapshot_.artwork_availability == ArtworkAvailability::kFailed) {
        this->StartIdentityTransition_(this->snapshot_, desired_key, now_ms, artwork);
      } else if (TextChanged(this->displayed_, this->snapshot_)) {
        this->StartTextTransition_(this->snapshot_, now_ms);
      } else {
        this->displayed_ = this->snapshot_;
      }
    }
  }

  this->AdvanceDim_(now_ms);
}

}  // namespace pixoo::now_playing
