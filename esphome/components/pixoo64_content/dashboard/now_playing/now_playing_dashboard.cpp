#include "now_playing_dashboard.h"

#include <algorithm>
#include <cstring>

#ifdef ESP_PLATFORM
#include "esp_attr.h"
#endif

namespace esphome::pixoo64::dashboard {
namespace {

using pixoo::now_playing::ArtworkAvailability;
using pixoo::now_playing::NowPlayingData;
using pixoo::now_playing::NowPlayingSourceState;
using pixoo::now_playing::PlaybackState;

constexpr uint64_t kVisualArtworkDomain = 0x56495355414c4152ull;
constexpr uint64_t kVisualMediaDomain = 0x56495355414c4d45ull;
constexpr uint64_t kVisualStateDomain = 0x56495355414c5354ull;
constexpr uint64_t kTitleDomain = 0x4d4152515449544cull;
constexpr uint64_t kArtistDomain = 0x4d41525141525449ull;

uint64_t DomainKey(uint64_t domain, uint64_t value) {
  return pixoo::now_playing::HashNowPlayingBytes(domain, &value,
                                                  sizeof(value));
}

bool ShowsMetadata(const NowPlayingData &data) {
  return (data.source_state == NowPlayingSourceState::kReady ||
          data.source_state == NowPlayingSourceState::kStale) &&
         pixoo::now_playing::IsActivePlaybackState(data.playback_state);
}

bool SameTrackPresentation(const NowPlayingData &left,
                           const NowPlayingData &right) {
  return left.media_identity == right.media_identity &&
         left.title.size == right.title.size &&
         std::memcmp(left.title.bytes, right.title.bytes, left.title.size) == 0 &&
         left.artist.size == right.artist.size &&
         std::memcmp(left.artist.bytes, right.artist.bytes, left.artist.size) == 0;
}

uint8_t MixChannel(uint8_t from, uint8_t to, uint8_t amount) {
  return static_cast<uint8_t>(
      (static_cast<uint32_t>(from) * (255u - amount) +
       static_cast<uint32_t>(to) * amount + 127u) /
      255u);
}

uint8_t ScaleChannel(uint8_t value, uint8_t amount) {
  return static_cast<uint8_t>((static_cast<uint16_t>(value) * amount + 127u) /
                              255u);
}

}  // namespace

#ifdef ESP_PLATFORM
DRAM_ATTR
#endif
uint16_t NowPlayingDashboard::transition_buffers_[2]
                                                    [pixoo::now_playing::kArtworkPixelCount]{};

void NowPlayingDashboard::OnShow(uint32_t now_ms) {
  if (this->hidden_) {
    const uint32_t hidden_ms = now_ms - this->hidden_started_ms_;
    if (this->metadata_rows_initialized_)
      this->metadata_rows_started_ms_ += hidden_ms;
    this->title_marquee_.Delay(hidden_ms);
    this->artist_marquee_.Delay(hidden_ms);
    this->hidden_ = false;
  }
  if (this->source_ != nullptr)
    this->source_->SetArtworkEligible(true, now_ms);
}

void NowPlayingDashboard::OnHide(uint32_t now_ms) {
  this->hidden_started_ms_ = now_ms;
  this->hidden_ = true;
  if (this->source_ != nullptr)
    this->source_->SetArtworkEligible(false, now_ms);
}

uint64_t NowPlayingDashboard::VisualKey_(const NowPlayingData &data) {
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

bool NowPlayingDashboard::ReadyArtwork_(const NowPlayingData &data,
                                        uint64_t identity,
                                        uint32_t revision) {
  return data.has_artwork_identity &&
         data.artwork_availability == ArtworkAvailability::kReady &&
         data.artwork_identity == identity &&
         data.artwork_revision == revision;
}

uint8_t NowPlayingDashboard::DesiredDim_(const NowPlayingData &data) {
  if (data.source_state == NowPlayingSourceState::kStale)
    return 112;
  if (data.playback_state == PlaybackState::kPaused)
    return 150;
  return 255;
}

uint8_t NowPlayingDashboard::Red_(uint16_t pixel) {
  return static_cast<uint8_t>(((pixel >> 11) & 0x1f) * 255 / 31);
}

uint8_t NowPlayingDashboard::Green_(uint16_t pixel) {
  return static_cast<uint8_t>(((pixel >> 5) & 0x3f) * 255 / 63);
}

uint8_t NowPlayingDashboard::Blue_(uint16_t pixel) {
  return static_cast<uint8_t>((pixel & 0x1f) * 255 / 31);
}

void NowPlayingDashboard::InitializeVisual_(uint64_t visual_key,
                                            uint32_t now_ms) {
  (void) now_ms;
  this->front_buffer_ = 0;
  this->transition_buffer_ = 1;
  pixoo::now_playing::GenerateArtworkPlaceholder(
      visual_key, transition_buffers_[this->front_buffer_],
      pixoo::now_playing::kArtworkPixelCount);
  this->buffer_info_[this->front_buffer_] = {
      visual_key, 0, 0, BufferKind::kPlaceholder};
  this->displayed_ = this->snapshot_;
  this->initialized_ = true;

  if (!this->snapshot_.has_artwork_identity ||
      this->snapshot_.artwork_availability != ArtworkAvailability::kReady)
    return;
  if (this->source_->CopyArtwork(
          this->snapshot_.artwork_identity,
          this->snapshot_.artwork_revision,
          transition_buffers_[this->front_buffer_],
          pixoo::now_playing::kArtworkPixelCount)) {
    this->buffer_info_[this->front_buffer_] = {
        visual_key, this->snapshot_.artwork_identity,
        this->snapshot_.artwork_revision, BufferKind::kArtwork};
  }
}

void NowPlayingDashboard::StartIdentityTransition_(uint64_t visual_key,
                                                    uint32_t now_ms,
                                                    bool immediate) {
  this->transition_buffer_ = static_cast<int8_t>(1 - this->front_buffer_);
  pixoo::now_playing::GenerateArtworkPlaceholder(
      visual_key, transition_buffers_[this->transition_buffer_],
      pixoo::now_playing::kArtworkPixelCount);
  this->buffer_info_[this->transition_buffer_] = {
      visual_key, 0, 0, BufferKind::kPlaceholder};
  this->displayed_ = this->snapshot_;
  if (immediate) {
    this->front_buffer_ = this->transition_buffer_;
    this->transition_buffer_ = static_cast<int8_t>(1 - this->front_buffer_);
    this->transition_kind_ = TransitionKind::kNone;
    this->crossfade_ = 0;
    return;
  }
  this->transition_kind_ = TransitionKind::kIdentityPlaceholder;
  this->crossfade_ = 0;
  this->visual_timeline_.Start(now_ms, kIdentityTransitionMs);
}

bool NowPlayingDashboard::StartArtworkTransition_(
    const NowPlayingData &data, uint64_t visual_key, uint32_t now_ms,
    bool immediate) {
  if (!data.has_artwork_identity ||
      data.artwork_availability != ArtworkAvailability::kReady)
    return false;
  this->transition_buffer_ = static_cast<int8_t>(1 - this->front_buffer_);
  if (!this->source_->CopyArtwork(
          data.artwork_identity, data.artwork_revision,
          transition_buffers_[this->transition_buffer_],
          pixoo::now_playing::kArtworkPixelCount))
    return false;
  const BufferInfo info{visual_key, data.artwork_identity,
                        data.artwork_revision, BufferKind::kArtwork};
  this->buffer_info_[this->transition_buffer_] = info;
  if (immediate) {
    this->front_buffer_ = this->transition_buffer_;
    this->transition_buffer_ = static_cast<int8_t>(1 - this->front_buffer_);
    this->transition_kind_ = TransitionKind::kNone;
    this->crossfade_ = 0;
    return true;
  }
  if (std::memcmp(transition_buffers_[this->front_buffer_],
                  transition_buffers_[this->transition_buffer_],
                  pixoo::now_playing::kArtworkRgb565Bytes) == 0) {
    this->buffer_info_[this->front_buffer_] = info;
    this->transition_kind_ = TransitionKind::kNone;
    this->crossfade_ = 0;
    return true;
  }
  this->transition_kind_ = TransitionKind::kArtwork;
  this->crossfade_ = 0;
  this->visual_timeline_.Start(now_ms, kArtworkTransitionMs);
  return true;
}

void NowPlayingDashboard::CancelTransition_() {
  this->transition_kind_ = TransitionKind::kNone;
  this->crossfade_ = 0;
}

void NowPlayingDashboard::AdvanceTransition_(uint32_t now_ms) {
  if (this->transition_kind_ == TransitionKind::kNone)
    return;
  const BufferInfo &target = this->buffer_info_[this->transition_buffer_];
  const uint64_t desired_key = VisualKey_(this->snapshot_);
  if (target.visual_key != desired_key ||
      (this->transition_kind_ == TransitionKind::kArtwork &&
       !ReadyArtwork_(this->snapshot_, target.artwork_identity,
                      target.artwork_revision))) {
    if (target.visual_key == VisualKey_(this->displayed_)) {
      this->front_buffer_ = this->transition_buffer_;
      this->transition_buffer_ = static_cast<int8_t>(1 - this->front_buffer_);
    }
    this->CancelTransition_();
    return;
  }
  if (!this->visual_timeline_.Complete(now_ms)) {
    this->crossfade_ = this->visual_timeline_.Smooth(now_ms);
    return;
  }

  this->front_buffer_ = this->transition_buffer_;
  this->transition_buffer_ = static_cast<int8_t>(1 - this->front_buffer_);
  this->transition_kind_ = TransitionKind::kNone;
  this->crossfade_ = 0;
}

void NowPlayingDashboard::AdvanceDim_(uint32_t now_ms) {
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

int NowPlayingDashboard::Measure_(const char *text) const {
  if (this->font_ == nullptr || text == nullptr || text[0] == '\0')
    return 0;
  int width = 0;
  int x_offset = 0;
  int baseline = 0;
  int height = 0;
  this->font_->measure(text, &width, &x_offset, &baseline, &height);
  return width;
}

void NowPlayingDashboard::UpdateTextLayout_(uint32_t now_ms) {
  if (!ShowsMetadata(this->displayed_)) {
    this->title_width_ = 0;
    this->artist_width_ = 0;
    this->title_offset_ = 0;
    this->artist_offset_ = 0;
    this->metadata_rows_initialized_ = false;
    this->metadata_rows_visible_ = false;
    return;
  }
  const char *title = this->displayed_.title.size == 0
                          ? "NOW PLAYING"
                          : this->displayed_.title.bytes;
  const char *artist = this->displayed_.artist.bytes;
  this->title_width_ = this->Measure_(title);
  this->artist_width_ = this->Measure_(artist);
  const uint64_t title_text_identity =
      pixoo::now_playing::HashNowPlayingBytes(
          kTitleDomain, title, this->displayed_.title.size == 0
                                   ? sizeof("NOW PLAYING") - 1
                                   : this->displayed_.title.size);
  const uint64_t artist_text_identity =
      pixoo::now_playing::HashNowPlayingBytes(
          kArtistDomain, artist, this->displayed_.artist.size);
  const uint64_t title_identity = pixoo::now_playing::HashNowPlayingBytes(
      title_text_identity, &this->displayed_.media_identity,
      sizeof(this->displayed_.media_identity));
  const uint64_t artist_identity = pixoo::now_playing::HashNowPlayingBytes(
      artist_text_identity, &this->displayed_.media_identity,
      sizeof(this->displayed_.media_identity));
  if (!this->metadata_rows_initialized_ ||
      this->metadata_rows_media_identity_ != this->displayed_.media_identity ||
      this->metadata_rows_title_identity_ != title_identity ||
      this->metadata_rows_artist_identity_ != artist_identity) {
    this->metadata_rows_started_ms_ = now_ms;
    this->metadata_rows_media_identity_ = this->displayed_.media_identity;
    this->metadata_rows_title_identity_ = title_identity;
    this->metadata_rows_artist_identity_ = artist_identity;
    this->metadata_rows_initialized_ = true;
    this->metadata_rows_visible_ = true;
  }
  this->title_offset_ = this->title_marquee_.Offset(
      title_identity, this->title_width_, kTextWidth, now_ms,
      kMarqueePauseMs, kMarqueeStepMs, kMarqueeGapPx);
  this->artist_offset_ = this->artist_marquee_.Offset(
      artist_identity, this->artist_width_, kTextWidth, now_ms,
      kMarqueePauseMs, kMarqueeStepMs, kMarqueeGapPx);

  const bool title_complete =
      this->title_width_ <= kTextWidth ||
      this->title_marquee_.CompletedCycles(
          title_identity, this->title_width_, kTextWidth, now_ms,
          kMarqueePauseMs, kMarqueeStepMs, kMarqueeGapPx) >=
          kMetadataRowsMinimumCycles;
  const bool artist_complete =
      this->artist_width_ <= kTextWidth ||
      this->artist_marquee_.CompletedCycles(
          artist_identity, this->artist_width_, kTextWidth, now_ms,
          kMarqueePauseMs, kMarqueeStepMs, kMarqueeGapPx) >=
          kMetadataRowsMinimumCycles;
  this->metadata_rows_visible_ =
      now_ms - this->metadata_rows_started_ms_ < kMetadataRowsMinimumMs ||
      !title_complete || !artist_complete;
}

void NowPlayingDashboard::Tick(uint32_t now_ms) {
  this->current_ms_ = now_ms;
  if (!this->available())
    return;

  // The source boundary is sampled exactly once. Every decision and every
  // rendered value in this frame is derived from this retained copy.
  this->snapshot_ = this->source_->Data();
  const uint64_t desired_key = VisualKey_(this->snapshot_);
  if (!this->initialized_) {
    this->InitializeVisual_(desired_key, now_ms);
  } else {
    this->AdvanceTransition_(now_ms);
    if (this->transition_kind_ == TransitionKind::kNone) {
      const BufferInfo &front = this->buffer_info_[this->front_buffer_];
      const bool pending_artwork =
          ShowsMetadata(this->displayed_) &&
          this->snapshot_.has_artwork_identity &&
          this->snapshot_.artwork_availability == ArtworkAvailability::kPending;
      if (pending_artwork) {
        // Keep the previous media and artwork together until the replacement
        // is ready or has failed.
      } else if (front.visual_key != desired_key) {
        if (this->snapshot_.has_artwork_identity &&
            this->snapshot_.artwork_availability ==
                ArtworkAvailability::kReady) {
          const bool presentation_changed =
              !SameTrackPresentation(this->displayed_, this->snapshot_);
          this->displayed_ = this->snapshot_;
          if (!this->StartArtworkTransition_(this->snapshot_, desired_key,
                                             now_ms, presentation_changed))
            this->StartIdentityTransition_(desired_key, now_ms, true);
        } else {
          this->StartIdentityTransition_(
              desired_key, now_ms,
              this->snapshot_.artwork_availability ==
                  ArtworkAvailability::kFailed);
        }
      } else if (this->snapshot_.has_artwork_identity &&
                 this->snapshot_.artwork_availability ==
                     ArtworkAvailability::kReady &&
                 !(front.kind == BufferKind::kArtwork &&
                   front.artwork_identity == this->snapshot_.artwork_identity &&
                   front.artwork_revision ==
                       this->snapshot_.artwork_revision)) {
        const bool presentation_changed =
            !SameTrackPresentation(this->displayed_, this->snapshot_);
        this->displayed_ = this->snapshot_;
        if (!this->StartArtworkTransition_(this->snapshot_, desired_key, now_ms,
                                           presentation_changed))
          this->StartIdentityTransition_(desired_key, now_ms, true);
      } else if (front.kind == BufferKind::kArtwork &&
                 this->snapshot_.has_artwork_identity &&
                 this->snapshot_.artwork_availability ==
                     ArtworkAvailability::kFailed) {
        this->StartIdentityTransition_(desired_key, now_ms, true);
      } else {
        this->displayed_ = this->snapshot_;
      }
    } else if (!(ShowsMetadata(this->displayed_) &&
                 this->snapshot_.has_artwork_identity &&
                 this->snapshot_.artwork_availability ==
                     ArtworkAvailability::kPending)) {
      // Playback corrections continue to follow accepted source updates while
      // a visual transition is active.
      this->displayed_ = this->snapshot_;
    }
  }

  this->AdvanceDim_(now_ms);
  this->UpdateTextLayout_(now_ms);
}

void NowPlayingDashboard::DrawBackground_(display::Display &display) const {
  const uint16_t *front = transition_buffers_[this->front_buffer_];
  const uint16_t *target =
      this->transition_kind_ == TransitionKind::kNone
          ? front
          : transition_buffers_[this->transition_buffer_];
  const uint8_t amount = this->transition_kind_ == TransitionKind::kNone
                             ? 0
                             : this->crossfade_;
  for (int y = 0; y < 64; ++y) {
    uint8_t lower_third = 255;
    if (y >= 39) {
      const int row = y - 39;
      lower_third = static_cast<uint8_t>(
          std::max(35, 238 - row * 8));
    }
    const uint8_t level = static_cast<uint8_t>(
        (static_cast<uint16_t>(this->dim_value_) * lower_third + 127u) /
        255u);
    for (int x = 0; x < 64; ++x) {
      const size_t index = static_cast<size_t>(y) * 64u + x;
      const uint16_t a = front[index];
      const uint16_t b = target[index];
      const uint8_t red = ScaleChannel(
          MixChannel(Red_(a), Red_(b), amount), level);
      const uint8_t green = ScaleChannel(
          MixChannel(Green_(a), Green_(b), amount), level);
      const uint8_t blue = ScaleChannel(
          MixChannel(Blue_(a), Blue_(b), amount), level);
      display.draw_pixel_at(x, y, Color(red, green, blue));
    }
  }
}

void NowPlayingDashboard::DrawRow_(display::Display &display, const char *text,
                                   int width, int offset, int y,
                                   Color color) const {
  if (text == nullptr || text[0] == '\0' || width <= 0)
    return;
  display.start_clipping(kTextLeft, y, kTextRight, y + 8);
  const int x = kTextLeft - offset;
  const auto print_pair = [&](int origin, int row, Color ink) {
    display.print(origin, row, this->font_, ink, display::TextAlign::TOP_LEFT,
                  text);
    if (width > kTextWidth)
      display.print(origin + width + kMarqueeGapPx, row, this->font_, ink,
                    display::TextAlign::TOP_LEFT, text);
  };
  print_pair(x + 1, y + 1, Color(3, 7, 12));
  print_pair(x, y, color);
  display.end_clipping();
}

void NowPlayingDashboard::DrawRows_(display::Display &display) const {
  if (!ShowsMetadata(this->displayed_) || !this->metadata_rows_visible_)
    return;
  const char *title = this->displayed_.title.size == 0
                          ? "NOW PLAYING"
                          : this->displayed_.title.bytes;
  this->DrawRow_(display, title, this->title_width_, this->title_offset_, 45,
                 Color(255, 255, 255));
  this->DrawRow_(display, this->displayed_.artist.bytes,
                 this->artist_width_, this->artist_offset_, 53,
                 Color(188, 210, 222));
}

void NowPlayingDashboard::DrawStatusMessage_(display::Display &display) const {
  if (ShowsMetadata(this->displayed_))
    return;
  const char *top = "NOW";
  const char *bottom = "PLAYING";
  Color top_color(255, 255, 255);
  Color bottom_color(188, 210, 222);
  switch (this->displayed_.source_state) {
    case NowPlayingSourceState::kUnconfigured:
      top = "SET UP";
      bottom = "NOW PLAYING";
      break;
    case NowPlayingSourceState::kWaiting:
      top = "LOADING";
      bottom = "NOW PLAYING";
      break;
    case NowPlayingSourceState::kNoEntityData:
      top = "NO ENTITY";
      bottom = "DATA";
      top_color = Color(255, 184, 62);
      bottom_color = Color(210, 132, 35);
      break;
    case NowPlayingSourceState::kOffline:
      top = "OFFLINE";
      bottom = "NOW PLAYING";
      top_color = Color(255, 184, 62);
      bottom_color = Color(210, 132, 35);
      break;
    case NowPlayingSourceState::kReady:
      if (pixoo::now_playing::IsInactivePlaybackState(
              this->displayed_.playback_state)) {
        top = "NOTHING";
        bottom = "PLAYING";
      }
      break;
    case NowPlayingSourceState::kStale:
      break;
  }
  display.print(33, 27, this->font_, Color(3, 7, 12),
                display::TextAlign::TOP_CENTER, top);
  display.print(32, 26, this->font_, top_color,
                display::TextAlign::TOP_CENTER, top);
  display.print(33, 36, this->font_, Color(3, 7, 12),
                display::TextAlign::TOP_CENTER, bottom);
  display.print(32, 35, this->font_, bottom_color,
                display::TextAlign::TOP_CENTER, bottom);
}

void NowPlayingDashboard::DrawMarks_(display::Display &display) const {
  if (this->displayed_.playback_state == PlaybackState::kPaused &&
      ShowsMetadata(this->displayed_)) {
    display.filled_rectangle(55, 4, 2, 7, Color(232, 241, 245));
    display.filled_rectangle(59, 4, 2, 7, Color(232, 241, 245));
  }

  const bool loading =
      this->displayed_.playback_state == PlaybackState::kBuffering ||
      this->displayed_.source_state == NowPlayingSourceState::kWaiting;
  if (loading) {
    static constexpr int8_t points[4][2] = {
        {58, 3}, {61, 6}, {58, 9}, {55, 6}};
    const uint8_t active = static_cast<uint8_t>((this->current_ms_ / 150) % 4);
    for (uint8_t i = 0; i < 4; ++i) {
      const Color color = i == active ? Color(245, 252, 255)
                                      : Color(65, 83, 94);
      display.filled_rectangle(points[i][0], points[i][1], 2, 2, color);
    }
  }

  if (this->displayed_.source_state == NowPlayingSourceState::kStale) {
    const Color amber(255, 174, 42);
    display.filled_rectangle(58, 3, 2, 5, amber);
    display.filled_rectangle(58, 10, 2, 2, amber);
  }

  if (ShowsMetadata(this->displayed_) &&
      this->displayed_.has_artwork_identity &&
      this->displayed_.artwork_availability == ArtworkAvailability::kFailed) {
    const Color amber(255, 174, 42);
    display.rectangle(3, 3, 8, 7, amber);
    display.line(4, 8, 6, 6, amber);
    display.line(6, 6, 9, 9, amber);
    display.filled_rectangle(12, 3, 2, 5, amber);
    display.filled_rectangle(12, 10, 2, 2, amber);
  }
}

void NowPlayingDashboard::DrawProgress_(display::Display &display) const {
  if (!ShowsMetadata(this->displayed_))
    return;
  const pixoo::now_playing::ProgressEstimate progress =
      pixoo::now_playing::EstimateProgress(this->displayed_, this->current_ms_);
  if (!progress.visible || progress.duration_ms == 0)
    return;
  display.horizontal_line(0, 63, 64, Color(20, 31, 37));
  int width = static_cast<int>(
      (static_cast<uint64_t>(progress.position_ms) * 64u) /
      progress.duration_ms);
  width = std::clamp(width, 0, 64);
  if (progress.position_ms != 0 && width == 0)
    width = 1;
  if (width > 0)
    display.horizontal_line(0, 63, width, Color(70, 225, 255));
}

void NowPlayingDashboard::Render(display::Display &display) const {
  if (!this->initialized_)
    return;
  this->DrawBackground_(display);
  this->DrawStatusMessage_(display);
  this->DrawRows_(display);
  this->DrawMarks_(display);
  this->DrawProgress_(display);
}

}  // namespace esphome::pixoo64::dashboard
