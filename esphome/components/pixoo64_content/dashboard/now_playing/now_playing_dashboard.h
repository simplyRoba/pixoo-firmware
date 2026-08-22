#pragma once

#include <cstddef>
#include <cstdint>

#include "dashboard/dashboard.h"
#include "esphome/components/font/font.h"
#include "now_playing_art.h"
#include "now_playing_data.h"
#include "now_playing_source.h"
#include "now_playing_timing.h"

namespace esphome::pixoo64::dashboard {

class NowPlayingDashboard final : public Dashboard {
 public:
  void set_source(pixoo::now_playing::NowPlayingSource *source) {
    this->source_ = source;
  }
  void set_font(font::Font *font) { this->font_ = font; }

  bool available() const override {
    return this->source_ != nullptr && this->font_ != nullptr;
  }
  void Prepare(uint32_t now_ms) override;
  void CancelPreparation(uint32_t now_ms) override;
  bool ReadyToShow() const override;
  void OnShow(uint32_t now_ms) override;
  void OnHide(uint32_t now_ms) override;
  void Tick(uint32_t now_ms) override;
  void Render(display::Display &display) const override;

 private:
  enum class BufferKind : uint8_t { kPlaceholder, kArtwork };
  enum class TransitionKind : uint8_t {
    kNone,
    kIdentityPlaceholder,
    kArtwork,
    kText,
  };

  struct BufferInfo {
    uint64_t visual_key{0};
    uint64_t artwork_identity{0};
    uint32_t artwork_revision{0};
    BufferKind kind{BufferKind::kPlaceholder};
    uint8_t average_red{0};
    uint8_t average_green{0};
    uint8_t average_blue{0};
  };

  static constexpr uint32_t kIdentityTransitionMs = 600;
  static constexpr uint32_t kArtworkTransitionMs = 700;
  static constexpr uint32_t kDimTransitionMs = 400;
  static constexpr uint32_t kMarqueePauseMs = 900;
  static constexpr uint32_t kMarqueeStepMs = 80;
  static constexpr uint8_t kProgressMinimumLuma = 96;
  static constexpr int kMarqueeGapPx = 12;
  static constexpr int kTextLeft = 2;
  static constexpr int kTextRight = 62;
  static constexpr int kTextWidth = kTextRight - kTextLeft;
  static constexpr int kTextRowHeight = 8;
  static constexpr size_t kTextMaskPixels = 64u * kTextRowHeight;

  static uint64_t VisualKey_(const pixoo::now_playing::NowPlayingData &data);
  static uint8_t DesiredDim_(
      const pixoo::now_playing::NowPlayingData &data);
  static uint8_t Red_(uint16_t pixel);
  static uint8_t Green_(uint16_t pixel);
  static uint8_t Blue_(uint16_t pixel);

  void SetArtworkEligible_(bool eligible, uint32_t now_ms);
  void ResetPresentation_();
  void InitializeVisual_(uint64_t visual_key, uint32_t now_ms);
  void StartIdentityTransition_(
      const pixoo::now_playing::NowPlayingData &data, uint64_t visual_key,
      uint32_t now_ms);
  bool StartArtworkTransition_(
      const pixoo::now_playing::NowPlayingData &data, uint64_t visual_key,
      uint32_t now_ms);
  void StartTextTransition_(const pixoo::now_playing::NowPlayingData &data,
                            uint32_t now_ms);
  void StageMetadata_(const pixoo::now_playing::NowPlayingData &data);
  bool TransitionMatches_(const pixoo::now_playing::NowPlayingData &data) const;
  void CancelTransition_();
  void AdvanceTransition_(uint32_t now_ms);
  void AdvanceDim_(uint32_t now_ms);
  void UpdateBufferAverage_(int8_t buffer);
  Color ProgressColor_() const;
  int Measure_(const char *text) const;
  void UpdateTextLayout_(uint32_t now_ms);
  void RasterizeRows_();
  void RasterizeRow_(uint8_t *mask, const char *text, int width,
                     int offset);
  void RasterizeText_(uint8_t *mask, const char *text, int origin);
  void DrawBackground_(display::Display &display) const;
  void DrawRows_(display::Display &display) const;
  void DrawRow_(display::Display &display, const uint8_t *mask, int y,
                Color color) const;
  void DrawStatusMessage_(display::Display &display) const;
  void DrawMarks_(display::Display &display) const;
  void DrawProgress_(display::Display &display) const;

  pixoo::now_playing::NowPlayingSource *source_{nullptr};
  font::Font *font_{nullptr};
  pixoo::now_playing::NowPlayingData snapshot_{};
  pixoo::now_playing::NowPlayingData displayed_{};
  pixoo::now_playing::NowPlayingData transition_from_data_{};
  pixoo::now_playing::NowPlayingData transition_data_{};
  pixoo::now_playing::MarqueeTiming title_marquee_{};
  pixoo::now_playing::MarqueeTiming artist_marquee_{};
  pixoo::now_playing::TransitionTimeline visual_timeline_{};
  pixoo::now_playing::TransitionTimeline dim_timeline_{};
  BufferInfo buffer_info_[2]{};
  uint8_t title_mask_[kTextMaskPixels]{};
  uint8_t artist_mask_[kTextMaskPixels]{};
  uint32_t current_ms_{0};
  int title_width_{0};
  int artist_width_{0};
  int title_offset_{0};
  int artist_offset_{0};
  int8_t front_buffer_{0};
  int8_t transition_buffer_{1};
  TransitionKind transition_kind_{TransitionKind::kNone};
  uint8_t crossfade_{0};
  uint8_t text_opacity_{255};
  uint8_t dim_from_{255};
  uint8_t dim_value_{255};
  uint8_t dim_target_{255};
  bool initialized_{false};
  bool dim_transitioning_{false};
  bool text_transitioning_{false};
  bool text_data_switched_{false};
  bool hidden_{false};
  bool artwork_eligible_{false};

  // One configured now-playing renderer owns exactly these two 64x64 RGB565
  // buffers. Both sides of a crossfade are read on every frame, so production
  // defines this static storage in internal DRAM rather than PSRAM or the heap.
  static uint16_t transition_buffers_[2]
                                     [pixoo::now_playing::kArtworkPixelCount];
  static_assert(pixoo::now_playing::kArtworkRgb565Bytes == 8192,
                "one RGB565 transition buffer must be 8 KiB");
  static_assert(sizeof(transition_buffers_) == 16384,
                "the renderer must own exactly two transition buffers");
};

}  // namespace esphome::pixoo64::dashboard
