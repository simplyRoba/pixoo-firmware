#include "now_playing_dashboard.h"

#include <algorithm>
#include <cstring>

#include "esphome/components/pixoo64_content/blend_canvas.h"
#include "esphome/core/hal.h"

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

uint8_t ScaleChannel(uint8_t value, uint8_t amount) {
  return static_cast<uint8_t>((static_cast<uint16_t>(value) * amount + 127u) /
                              255u);
}

uint32_t NextCodepoint(const char *text, size_t *length) {
  const uint8_t *current = reinterpret_cast<const uint8_t *>(text);
  const uint8_t first = *current++;
  if (first == 0) {
    *length = 0;
    return 0;
  }
  if (first < 0x80) {
    *length = 1;
    return first;
  }
  uint32_t codepoint = 0;
  size_t count = 0;
  if ((first & 0xe0) == 0xc0) {
    codepoint = first & 0x1f;
    count = 1;
  } else if ((first & 0xf0) == 0xe0) {
    codepoint = first & 0x0f;
    count = 2;
  } else if ((first & 0xf8) == 0xf0) {
    codepoint = first & 0x07;
    count = 3;
  } else {
    *length = 0;
    return 0;
  }
  for (size_t i = 0; i < count; ++i) {
    const uint8_t next = *current++;
    if ((next & 0xc0) != 0x80) {
      *length = 0;
      return 0;
    }
    codepoint = (codepoint << 6) | (next & 0x3f);
  }
  *length = count + 1;
  return codepoint;
}

}  // namespace

#ifdef ESP_PLATFORM
DRAM_ATTR
#endif
uint16_t NowPlayingDashboard::transition_buffers_[2]
                                                    [pixoo::now_playing::kArtworkPixelCount]{};

void NowPlayingDashboard::SetArtworkEligible_(bool eligible, uint32_t now_ms) {
  if (this->source_ == nullptr || this->artwork_eligible_ == eligible)
    return;
  this->source_->SetArtworkEligible(eligible, now_ms);
  this->artwork_eligible_ = eligible;
}

void NowPlayingDashboard::Prepare(uint32_t now_ms) {
  this->SetArtworkEligible_(true, now_ms);
}

void NowPlayingDashboard::CancelPreparation(uint32_t now_ms) {
  this->SetArtworkEligible_(false, now_ms);
}

bool NowPlayingDashboard::ReadyToShow() const {
  if (!this->available() || !this->source_->SnapshotSettled())
    return false;
  const NowPlayingData data = this->source_->Data();
  if (data.source_state == NowPlayingSourceState::kUnconfigured)
    return true;
  if (data.source_state != NowPlayingSourceState::kReady ||
      (ShowsMetadata(data) && !data.artwork_known) ||
      (data.has_artwork_identity &&
       data.artwork_availability != ArtworkAvailability::kReady))
    return false;
  return true;
}

void NowPlayingDashboard::ResetPresentation_() {
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
  this->current_ms_ = 0;
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

void NowPlayingDashboard::OnShow(uint32_t now_ms) {
  if (this->hidden_)
    this->ResetPresentation_();
  this->hidden_ = false;
  this->SetArtworkEligible_(true, now_ms);
}

void NowPlayingDashboard::OnHide(uint32_t now_ms) {
  this->hidden_ = true;
  this->SetArtworkEligible_(false, now_ms);
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

void NowPlayingDashboard::UpdateBufferAverage_(int8_t buffer) {
  uint64_t red = 0;
  uint64_t green = 0;
  uint64_t blue = 0;
  for (size_t index = 0; index < pixoo::now_playing::kArtworkPixelCount;
       ++index) {
    const uint16_t pixel = transition_buffers_[buffer][index];
    red += Red_(pixel);
    green += Green_(pixel);
    blue += Blue_(pixel);
  }
  constexpr uint64_t count = pixoo::now_playing::kArtworkPixelCount;
  BufferInfo &info = this->buffer_info_[buffer];
  info.average_red = static_cast<uint8_t>((red + count / 2) / count);
  info.average_green = static_cast<uint8_t>((green + count / 2) / count);
  info.average_blue = static_cast<uint8_t>((blue + count / 2) / count);
}

Color NowPlayingDashboard::ProgressColor_() const {
  const BufferInfo &front = this->buffer_info_[this->front_buffer_];
  const bool blends_artwork =
      this->transition_kind_ == TransitionKind::kArtwork ||
      this->transition_kind_ == TransitionKind::kIdentityPlaceholder;
  const BufferInfo &target = blends_artwork
                                 ? this->buffer_info_[this->transition_buffer_]
                                 : front;
  const uint8_t amount = blends_artwork ? this->crossfade_ : 0;
  uint8_t red = MixChannel(front.average_red, target.average_red, amount);
  uint8_t green = MixChannel(front.average_green, target.average_green, amount);
  uint8_t blue = MixChannel(front.average_blue, target.average_blue, amount);
  const uint16_t luma = static_cast<uint16_t>(
      (54u * red + 183u * green + 19u * blue + 128u) / 256u);
  if (luma < kProgressMinimumLuma) {
    // Mixing toward white raises brightness while reducing the saturation that
    // would make a scaled dark color look neon.
    const uint8_t lift = static_cast<uint8_t>(
        ((kProgressMinimumLuma - luma) * 255u + (255u - luma) / 2u) /
        (255u - luma));
    red = MixChannel(red, 255, lift);
    green = MixChannel(green, 255, lift);
    blue = MixChannel(blue, 255, lift);
  }
  return Color(red, green, blue);
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
      visual_key, 0, 0, BufferKind::kPlaceholder, 0, 0, 0};
  this->UpdateBufferAverage_(this->front_buffer_);
  this->displayed_ = this->snapshot_;
  this->transition_from_data_ = this->snapshot_;
  this->transition_data_ = this->snapshot_;
  this->initialized_ = true;

  if (!this->snapshot_.has_artwork_identity ||
      this->snapshot_.artwork_availability != ArtworkAvailability::kReady)
    return;
  if (this->source_->CopyArtwork(
          this->snapshot_.artwork_identity, this->snapshot_.artwork_revision,
          transition_buffers_[this->front_buffer_],
          pixoo::now_playing::kArtworkPixelCount)) {
    this->buffer_info_[this->front_buffer_] = {
        visual_key, this->snapshot_.artwork_identity,
        this->snapshot_.artwork_revision, BufferKind::kArtwork, 0, 0, 0};
    this->UpdateBufferAverage_(this->front_buffer_);
  }
}

void NowPlayingDashboard::StageMetadata_(const NowPlayingData &data) {
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

void NowPlayingDashboard::StartIdentityTransition_(
    const NowPlayingData &data, uint64_t visual_key, uint32_t now_ms) {
  this->transition_buffer_ = static_cast<int8_t>(1 - this->front_buffer_);
  pixoo::now_playing::GenerateArtworkPlaceholder(
      visual_key, transition_buffers_[this->transition_buffer_],
      pixoo::now_playing::kArtworkPixelCount);
  this->buffer_info_[this->transition_buffer_] = {
      visual_key, 0, 0, BufferKind::kPlaceholder, 0, 0, 0};
  this->UpdateBufferAverage_(this->transition_buffer_);
  if (std::memcmp(transition_buffers_[this->front_buffer_],
                  transition_buffers_[this->transition_buffer_],
                  pixoo::now_playing::kArtworkRgb565Bytes) == 0) {
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

bool NowPlayingDashboard::StartArtworkTransition_(
    const NowPlayingData &data, uint64_t visual_key, uint32_t now_ms) {
  if (!data.has_artwork_identity ||
      data.artwork_availability != ArtworkAvailability::kReady)
    return false;
  this->transition_buffer_ = static_cast<int8_t>(1 - this->front_buffer_);
  if (!this->source_->CopyArtwork(
          data.artwork_identity, data.artwork_revision,
          transition_buffers_[this->transition_buffer_],
          pixoo::now_playing::kArtworkPixelCount))
    return false;
  this->buffer_info_[this->transition_buffer_] = {
      visual_key, data.artwork_identity, data.artwork_revision,
      BufferKind::kArtwork, 0, 0, 0};
  this->UpdateBufferAverage_(this->transition_buffer_);
  if (std::memcmp(transition_buffers_[this->front_buffer_],
                  transition_buffers_[this->transition_buffer_],
                  pixoo::now_playing::kArtworkRgb565Bytes) == 0) {
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

void NowPlayingDashboard::StartTextTransition_(const NowPlayingData &data,
                                               uint32_t now_ms) {
  this->StageMetadata_(data);
  if (!this->text_transitioning_)
    return;
  this->transition_kind_ = TransitionKind::kText;
  this->crossfade_ = 0;
  this->visual_timeline_.Start(now_ms, kArtworkTransitionMs);
}

bool NowPlayingDashboard::TransitionMatches_(const NowPlayingData &data) const {
  return VisualKey_(data) == VisualKey_(this->transition_data_) &&
         data.media_identity == this->transition_data_.media_identity &&
         data.has_artwork_identity == this->transition_data_.has_artwork_identity &&
         data.artwork_identity == this->transition_data_.artwork_identity &&
         data.artwork_availability == this->transition_data_.artwork_availability &&
         data.artwork_revision == this->transition_data_.artwork_revision &&
         !TextChanged(data, this->transition_data_);
}

void NowPlayingDashboard::CancelTransition_() {
  this->displayed_ = this->transition_from_data_;
  this->transition_kind_ = TransitionKind::kNone;
  this->crossfade_ = 0;
  this->text_transitioning_ = false;
  this->text_data_switched_ = false;
  this->text_opacity_ = 255;
}

void NowPlayingDashboard::AdvanceTransition_(uint32_t now_ms) {
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
    return;
  }
  const char *title = this->displayed_.title.size == 0
                          ? "NOW PLAYING"
                          : this->displayed_.title.bytes;
  const char *artist = this->displayed_.artist.bytes;
  this->title_width_ = this->Measure_(title);
  this->artist_width_ = this->Measure_(artist);
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

void NowPlayingDashboard::RasterizeText_(uint8_t *mask, const char *text,
                                         int origin) {
  if (mask == nullptr || text == nullptr || this->font_ == nullptr)
    return;
  int x_at = origin;
  for (;;) {
    size_t length = 0;
    const uint32_t codepoint = NextCodepoint(text, &length);
    if (length == 0)
      break;
    text += length;
    const font::Glyph *glyph = this->font_->find_glyph(codepoint);
    if (glyph == nullptr) {
      const auto &glyphs = this->font_->get_glyphs();
      if (glyphs.empty())
        continue;
      for (int y = 0; y < this->font_->get_height() && y < kTextRowHeight;
           ++y) {
        for (int x = 0; x < glyphs[0].advance; ++x) {
          const int target_x = x_at + x;
          if (target_x >= kTextLeft && target_x < kTextRight)
            mask[static_cast<size_t>(y) * 64u + target_x] = 255;
        }
      }
      x_at += glyphs[0].advance;
      continue;
    }
    const uint8_t bpp = this->font_->get_bpp();
    const uint8_t maximum = static_cast<uint8_t>((1u << bpp) - 1u);
    for (int y = 0; y < glyph->height; ++y) {
      const int target_y = glyph->offset_y + y;
      if (target_y < 0 || target_y >= kTextRowHeight)
        continue;
      for (int x = 0; x < glyph->width; ++x) {
        const int target_x = x_at + glyph->offset_x + x;
        if (target_x < kTextLeft || target_x >= kTextRight)
          continue;
        const size_t bit = static_cast<size_t>(y * glyph->width + x) * bpp;
        const uint8_t packed =
            progmem_read_byte(glyph->data + bit / 8u);
        const uint8_t value = static_cast<uint8_t>(
            (packed >> (8u - bpp - bit % 8u)) & maximum);
        const uint8_t coverage = static_cast<uint8_t>(
            (static_cast<uint16_t>(value) * 255u) / maximum);
        uint8_t &pixel =
            mask[static_cast<size_t>(target_y) * 64u + target_x];
        pixel = std::max(pixel, coverage);
      }
    }
    x_at += glyph->advance;
  }
}

void NowPlayingDashboard::RasterizeRow_(uint8_t *mask, const char *text,
                                        int width, int offset) {
  std::memset(mask, 0, kTextMaskPixels);
  if (text == nullptr || text[0] == '\0' || width <= 0)
    return;
  const int origin = kTextLeft - offset;
  this->RasterizeText_(mask, text, origin);
  if (width > kTextWidth)
    this->RasterizeText_(mask, text, origin + width + kMarqueeGapPx);
}

void NowPlayingDashboard::RasterizeRows_() {
  if (!ShowsMetadata(this->displayed_)) {
    std::memset(this->title_mask_, 0, sizeof(this->title_mask_));
    std::memset(this->artist_mask_, 0, sizeof(this->artist_mask_));
    return;
  }
  const char *title = this->displayed_.title.size == 0
                          ? "NOW PLAYING"
                          : this->displayed_.title.bytes;
  this->RasterizeRow_(this->title_mask_, title, this->title_width_,
                      this->title_offset_);
  this->RasterizeRow_(this->artist_mask_, this->displayed_.artist.bytes,
                      this->artist_width_, this->artist_offset_);
}

void NowPlayingDashboard::Tick(uint32_t now_ms) {
  this->current_ms_ = now_ms;
  if (!this->available())
    return;

  this->snapshot_ = this->source_->Data();
  const uint64_t desired_key = VisualKey_(this->snapshot_);
  if (!this->initialized_) {
    this->InitializeVisual_(desired_key, now_ms);
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
          if (!this->StartArtworkTransition_(this->snapshot_, desired_key, now_ms))
            this->StartIdentityTransition_(this->snapshot_, desired_key, now_ms);
        } else {
          this->StartIdentityTransition_(this->snapshot_, desired_key, now_ms);
        }
      } else if (this->snapshot_.has_artwork_identity &&
                 this->snapshot_.artwork_availability == ArtworkAvailability::kReady &&
                 !(front.kind == BufferKind::kArtwork &&
                   front.artwork_identity == this->snapshot_.artwork_identity &&
                   front.artwork_revision == this->snapshot_.artwork_revision)) {
        if (!this->StartArtworkTransition_(this->snapshot_, desired_key, now_ms))
          this->StartIdentityTransition_(this->snapshot_, desired_key, now_ms);
      } else if (front.kind == BufferKind::kArtwork &&
                 this->snapshot_.has_artwork_identity &&
                 this->snapshot_.artwork_availability == ArtworkAvailability::kFailed) {
        this->StartIdentityTransition_(this->snapshot_, desired_key, now_ms);
      } else if (TextChanged(this->displayed_, this->snapshot_)) {
        this->StartTextTransition_(this->snapshot_, now_ms);
      } else {
        this->displayed_ = this->snapshot_;
      }
    }
  }

  this->AdvanceDim_(now_ms);
  this->UpdateTextLayout_(now_ms);
  this->RasterizeRows_();
}

void NowPlayingDashboard::DrawBackground_(display::Display &display) const {
  const uint16_t *front = transition_buffers_[this->front_buffer_];
  const uint16_t *target = this->transition_kind_ == TransitionKind::kArtwork ||
                                   this->transition_kind_ == TransitionKind::kIdentityPlaceholder
                               ? transition_buffers_[this->transition_buffer_]
                               : front;
  const uint8_t amount = target == front ? 0 : this->crossfade_;
  for (int y = 0; y < 64; ++y) {
    uint8_t lower_third = 255;
    if (y >= 39) {
      const int row = y - 39;
      lower_third = static_cast<uint8_t>(std::max(35, 238 - row * 8));
    }
    const uint8_t level = static_cast<uint8_t>(
        (static_cast<uint16_t>(this->dim_value_) * lower_third + 127u) / 255u);
    for (int x = 0; x < 64; ++x) {
      const size_t index = static_cast<size_t>(y) * 64u + x;
      const uint16_t a = front[index];
      const uint16_t b = target[index];
      display.draw_pixel_at(x, y, Color(
          ScaleChannel(MixChannel(Red_(a), Red_(b), amount), level),
          ScaleChannel(MixChannel(Green_(a), Green_(b), amount), level),
          ScaleChannel(MixChannel(Blue_(a), Blue_(b), amount), level)));
    }
  }
}

void NowPlayingDashboard::DrawRow_(display::Display &display, const uint8_t *mask,
                                   int y, Color color) const {
  content::BlendCanvas *canvas = content::BlendCanvasOf(display);
  if (mask == nullptr || this->text_opacity_ == 0)
    return;
  const float opacity = static_cast<float>(this->text_opacity_) / 255.0f;
  for (int row = 0; row < kTextRowHeight; ++row) {
    for (int x = kTextLeft; x < kTextRight; ++x) {
      const uint8_t coverage = mask[static_cast<size_t>(row) * 64u + x];
      if (coverage == 0)
        continue;
      const float alpha = opacity * static_cast<float>(coverage) / 255.0f;
      if (canvas != nullptr) {
        if (x + 1 < kTextRight && row + 1 < kTextRowHeight)
          canvas->BlendPixel(x + 1, y + row + 1, Color(3, 7, 12), alpha);
        canvas->BlendPixel(x, y + row, color, alpha);
      } else if (alpha >= 0.5f) {
        if (x + 1 < kTextRight && row + 1 < kTextRowHeight)
          display.draw_pixel_at(x + 1, y + row + 1, Color(3, 7, 12));
        display.draw_pixel_at(x, y + row, color);
      }
    }
  }
}

void NowPlayingDashboard::DrawRows_(display::Display &display) const {
  if (!ShowsMetadata(this->displayed_))
    return;
  this->DrawRow_(display, this->title_mask_, 45, Color(255, 255, 255));
  this->DrawRow_(display, this->artist_mask_, 53, Color(188, 210, 222));
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
      if (pixoo::now_playing::IsInactivePlaybackState(this->displayed_.playback_state)) {
        top = "NOTHING";
        bottom = "PLAYING";
      }
      break;
    case NowPlayingSourceState::kStale:
      break;
  }
  display.print(kTextLeft + 1, 46, this->font_, Color(3, 7, 12),
                display::TextAlign::TOP_LEFT, top);
  display.print(kTextLeft, 45, this->font_, top_color,
                display::TextAlign::TOP_LEFT, top);
  display.print(kTextLeft + 1, 54, this->font_, Color(3, 7, 12),
                display::TextAlign::TOP_LEFT, bottom);
  display.print(kTextLeft, 53, this->font_, bottom_color,
                display::TextAlign::TOP_LEFT, bottom);
}

void NowPlayingDashboard::DrawMarks_(display::Display &display) const {
  if (this->displayed_.playback_state == PlaybackState::kPaused &&
      ShowsMetadata(this->displayed_)) {
    display.filled_rectangle(55, 4, 2, 7, Color(232, 241, 245));
    display.filled_rectangle(59, 4, 2, 7, Color(232, 241, 245));
  }

  const bool loading = this->displayed_.playback_state == PlaybackState::kBuffering ||
                       this->displayed_.source_state == NowPlayingSourceState::kWaiting;
  if (loading) {
    static constexpr int8_t points[4][2] = {{58, 3}, {61, 6}, {58, 9}, {55, 6}};
    const uint8_t active = static_cast<uint8_t>((this->current_ms_ / 150) % 4);
    for (uint8_t i = 0; i < 4; ++i) {
      const Color color = i == active ? Color(245, 252, 255) : Color(65, 83, 94);
      display.filled_rectangle(points[i][0], points[i][1], 2, 2, color);
    }
  }

  if (this->displayed_.source_state == NowPlayingSourceState::kStale) {
    const Color amber(255, 174, 42);
    display.filled_rectangle(58, 3, 2, 5, amber);
    display.filled_rectangle(58, 10, 2, 2, amber);
  }

  if (ShowsMetadata(this->displayed_) && this->displayed_.has_artwork_identity &&
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
  int width = static_cast<int>((static_cast<uint64_t>(progress.position_ms) * 64u) /
                               progress.duration_ms);
  width = std::clamp(width, 0, 64);
  if (progress.position_ms != 0 && width == 0)
    width = 1;
  if (width > 0)
    display.horizontal_line(0, 63, width, this->ProgressColor_());
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
