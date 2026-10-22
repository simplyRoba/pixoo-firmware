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
using pixoo::now_playing::NowPlayingPresentation;

bool ShowsMetadata(const NowPlayingData &data) {
  return NowPlayingPresentation::ShowsMetadata(data);
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
  return NowPlayingPresentation::ReadyToShow(this->source_->Data(), true);
}

void NowPlayingDashboard::OnShow(uint32_t now_ms) {
  this->presentation_.OnShow();
  this->SetArtworkEligible_(true, now_ms);
}

void NowPlayingDashboard::OnHide(uint32_t now_ms) {
  this->presentation_.OnHide();
  this->SetArtworkEligible_(false, now_ms);
}

class NowPlayingDashboard::ArtworkBridge final
    : public pixoo::now_playing::NowPlayingPresentationArtwork {
 public:
  explicit ArtworkBridge(NowPlayingDashboard &owner) : owner_(owner) {}
  pixoo::now_playing::ArtworkAverage PreparePlaceholder(int8_t slot, uint64_t key) override {
    pixoo::now_playing::GenerateArtworkPlaceholder(
        key, transition_buffers_[slot], pixoo::now_playing::kArtworkPixelCount);
    return owner_.BufferAverage_(slot);
  }
  bool CopyArtwork(int8_t slot, uint64_t identity, uint32_t revision,
                   pixoo::now_playing::ArtworkAverage &average) override {
    if (!owner_.source_->CopyArtwork(identity, revision, transition_buffers_[slot],
                                    pixoo::now_playing::kArtworkPixelCount))
      return false;
    average = owner_.BufferAverage_(slot);
    return true;
  }
  bool EqualArtwork(int8_t left, int8_t right) const override {
    return std::memcmp(transition_buffers_[left], transition_buffers_[right],
                       pixoo::now_playing::kArtworkRgb565Bytes) == 0;
  }
 private:
  NowPlayingDashboard &owner_;
};

uint8_t NowPlayingDashboard::Red_(uint16_t pixel) {
  return static_cast<uint8_t>(((pixel >> 11) & 0x1f) * 255 / 31);
}

uint8_t NowPlayingDashboard::Green_(uint16_t pixel) {
  return static_cast<uint8_t>(((pixel >> 5) & 0x3f) * 255 / 63);
}

uint8_t NowPlayingDashboard::Blue_(uint16_t pixel) {
  return static_cast<uint8_t>((pixel & 0x1f) * 255 / 31);
}

pixoo::now_playing::ArtworkAverage NowPlayingDashboard::BufferAverage_(int8_t buffer) const {
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
  return {static_cast<uint8_t>((red + count / 2) / count),
          static_cast<uint8_t>((green + count / 2) / count),
          static_cast<uint8_t>((blue + count / 2) / count)};
}

Color NowPlayingDashboard::ProgressColor_() const {
  const BufferInfo &front = this->presentation_.Front();
  const bool blends_artwork =
      this->presentation_.Kind() == TransitionKind::kArtwork ||
      this->presentation_.Kind() == TransitionKind::kIdentityPlaceholder;
  const BufferInfo &target = blends_artwork
                                 ? this->presentation_.Target()
                                 : front;
  const uint8_t amount = blends_artwork ? this->presentation_.Crossfade() : 0;
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

int NowPlayingDashboard::Measure_(const char *text) const {
  if (this->font_ == nullptr || text == nullptr || text[0] == '\0')
    return 0;
  int width = 0;
  int x_offset = 0;
  int baseline = 0;
  int height = 0;
  this->font_->measure(text, &width, &x_offset, &baseline, &height);
  // RasterizeText_ advances its pen from the supplied origin. ESPHome reports
  // the first glyph's x offset separately, so both values form that span.
  return width + x_offset;
}

void NowPlayingDashboard::UpdateTextLayout_(uint32_t now_ms) {
  const auto &data = this->presentation_.Displayed();
  if (!ShowsMetadata(data)) {
    this->presentation_.UpdateTextLayout(0, 0, now_ms);
    return;
  }
  const char *title = data.title.size == 0 ? "NOW PLAYING" : data.title.bytes;
  this->presentation_.UpdateTextLayout(
      this->Measure_(title), this->Measure_(data.artist.bytes), now_ms);
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
  if (!ShowsMetadata(this->presentation_.Displayed())) {
    std::memset(this->title_mask_, 0, sizeof(this->title_mask_));
    std::memset(this->artist_mask_, 0, sizeof(this->artist_mask_));
    return;
  }
  const char *title = this->presentation_.Displayed().title.size == 0
                          ? "NOW PLAYING"
                          : this->presentation_.Displayed().title.bytes;
  this->RasterizeRow_(this->title_mask_, title, this->presentation_.TitleWidth(),
                      this->presentation_.TitleOffset());
  this->RasterizeRow_(this->artist_mask_, this->presentation_.Displayed().artist.bytes,
                      this->presentation_.ArtistWidth(), this->presentation_.ArtistOffset());
}

void NowPlayingDashboard::Tick(uint32_t now_ms) {
  this->current_ms_ = now_ms;
  if (!this->available())
    return;
  ArtworkBridge artwork(*this);
  this->presentation_.Update(this->source_->Data(), now_ms, artwork);
  this->UpdateTextLayout_(now_ms);
  this->RasterizeRows_();
}

void NowPlayingDashboard::DrawBackground_(display::Display &display) const {
  const uint16_t *front = transition_buffers_[this->presentation_.FrontIndex()];
  const uint16_t *target = this->presentation_.Kind() == TransitionKind::kArtwork ||
                                   this->presentation_.Kind() == TransitionKind::kIdentityPlaceholder
                               ? transition_buffers_[this->presentation_.TargetIndex()]
                               : front;
  const uint8_t amount = target == front ? 0 : this->presentation_.Crossfade();
  for (int y = 0; y < 64; ++y) {
    uint8_t lower_third = 255;
    if (y >= 39) {
      const int row = y - 39;
      lower_third = static_cast<uint8_t>(std::max(35, 255 - row * 8));
    }
    const uint8_t level = static_cast<uint8_t>(
        (static_cast<uint16_t>(this->presentation_.Dim()) * lower_third + 127u) / 255u);
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
  if (mask == nullptr || this->presentation_.TextOpacity() == 0)
    return;
  const float opacity = static_cast<float>(this->presentation_.TextOpacity()) / 255.0f;
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
  if (!ShowsMetadata(this->presentation_.Displayed()))
    return;
  this->DrawRow_(display, this->title_mask_, 45, Color(255, 255, 255));
  this->DrawRow_(display, this->artist_mask_, 53, Color(188, 210, 222));
}

void NowPlayingDashboard::DrawStatusMessage_(display::Display &display) const {
  if (ShowsMetadata(this->presentation_.Displayed()))
    return;
  const char *top = "NOW";
  const char *bottom = "PLAYING";
  Color top_color(255, 255, 255);
  Color bottom_color(188, 210, 222);
  switch (this->presentation_.Displayed().source_state) {
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
      if (pixoo::now_playing::IsInactivePlaybackState(this->presentation_.Displayed().playback_state)) {
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
  if (this->presentation_.Displayed().playback_state == PlaybackState::kPaused &&
      ShowsMetadata(this->presentation_.Displayed())) {
    display.filled_rectangle(55, 4, 2, 7, Color(232, 241, 245));
    display.filled_rectangle(59, 4, 2, 7, Color(232, 241, 245));
  }

  const bool loading = this->presentation_.Displayed().playback_state == PlaybackState::kBuffering ||
                       this->presentation_.Displayed().source_state == NowPlayingSourceState::kWaiting;
  if (loading) {
    static constexpr int8_t points[4][2] = {{58, 3}, {61, 6}, {58, 9}, {55, 6}};
    const uint8_t active = static_cast<uint8_t>((this->current_ms_ / 150) % 4);
    for (uint8_t i = 0; i < 4; ++i) {
      const Color color = i == active ? Color(245, 252, 255) : Color(65, 83, 94);
      display.filled_rectangle(points[i][0], points[i][1], 2, 2, color);
    }
  }

  if (this->presentation_.Displayed().source_state == NowPlayingSourceState::kStale) {
    const Color amber(255, 174, 42);
    display.filled_rectangle(58, 3, 2, 5, amber);
    display.filled_rectangle(58, 10, 2, 2, amber);
  }

  if (ShowsMetadata(this->presentation_.Displayed()) && this->presentation_.Displayed().has_artwork_identity &&
      this->presentation_.Displayed().artwork_availability == ArtworkAvailability::kFailed) {
    const Color amber(255, 174, 42);
    display.rectangle(3, 3, 8, 7, amber);
    display.line(4, 8, 6, 6, amber);
    display.line(6, 6, 9, 9, amber);
    display.filled_rectangle(12, 3, 2, 5, amber);
    display.filled_rectangle(12, 10, 2, 2, amber);
  }
}

void NowPlayingDashboard::DrawProgress_(display::Display &display) const {
  if (!ShowsMetadata(this->presentation_.Displayed()))
    return;
  const pixoo::now_playing::ProgressEstimate progress =
      pixoo::now_playing::EstimateProgress(this->presentation_.Displayed(), this->current_ms_);
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
  if (!this->presentation_.Initialized())
    return;
  this->DrawBackground_(display);
  this->DrawStatusMessage_(display);
  this->DrawRows_(display);
  this->DrawMarks_(display);
  this->DrawProgress_(display);
}

}  // namespace esphome::pixoo64::dashboard
