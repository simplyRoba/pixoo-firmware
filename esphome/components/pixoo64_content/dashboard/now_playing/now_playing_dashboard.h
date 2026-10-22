#pragma once

#include <cstddef>
#include <cstdint>

#include "dashboard/dashboard.h"
#include "esphome/components/font/font.h"
#include "now_playing_art.h"
#include "now_playing_source.h"
#include "now_playing_presentation.h"

namespace esphome::pixoo64::dashboard {

class NowPlayingDashboard final : public Dashboard {
 public:
  void set_source(pixoo::now_playing::NowPlayingSource *source) { source_ = source; }
  void set_font(font::Font *font) { font_ = font; }
  bool available() const override { return source_ != nullptr && font_ != nullptr; }
  void Prepare(uint32_t now_ms) override;
  void CancelPreparation(uint32_t now_ms) override;
  bool ReadyToShow() const override;
  void OnShow(uint32_t now_ms) override;
  void OnHide(uint32_t now_ms) override;
  void Tick(uint32_t now_ms) override;
  void Render(display::Display &display) const override;

 private:
  using TransitionKind = pixoo::now_playing::NowPlayingPresentation::TransitionKind;
  using BufferInfo = pixoo::now_playing::NowPlayingPresentation::BufferInfo;
  class ArtworkBridge;
  static constexpr uint8_t kProgressMinimumLuma = 96;
  static constexpr int kMarqueeGapPx = 12;
  static constexpr int kTextLeft = 2;
  static constexpr int kTextRight = 62;
  static constexpr int kTextWidth = kTextRight - kTextLeft;
  static constexpr int kTextRowHeight = 8;
  static constexpr size_t kTextMaskPixels = 64u * kTextRowHeight;
  static uint8_t Red_(uint16_t pixel);
  static uint8_t Green_(uint16_t pixel);
  static uint8_t Blue_(uint16_t pixel);
  void SetArtworkEligible_(bool eligible, uint32_t now_ms);
  pixoo::now_playing::ArtworkAverage BufferAverage_(int8_t buffer) const;
  Color ProgressColor_() const;
  int Measure_(const char *text) const;
  void UpdateTextLayout_(uint32_t now_ms);
  void RasterizeRows_();
  void RasterizeRow_(uint8_t *mask, const char *text, int width, int offset);
  void RasterizeText_(uint8_t *mask, const char *text, int origin);
  void DrawBackground_(display::Display &display) const;
  void DrawRows_(display::Display &display) const;
  void DrawRow_(display::Display &display, const uint8_t *mask, int y, Color color) const;
  void DrawStatusMessage_(display::Display &display) const;
  void DrawMarks_(display::Display &display) const;
  void DrawProgress_(display::Display &display) const;

  pixoo::now_playing::NowPlayingSource *source_{nullptr};
  font::Font *font_{nullptr};
  pixoo::now_playing::NowPlayingPresentation presentation_{};
  uint8_t title_mask_[kTextMaskPixels]{};
  uint8_t artist_mask_[kTextMaskPixels]{};
  uint32_t current_ms_{0};
  bool artwork_eligible_{false};

  // One configured now-playing renderer owns exactly these two 64x64 RGB565
  // buffers. Both sides of a crossfade are read on every frame, so production
  // defines this static storage in internal DRAM rather than PSRAM or the heap.
  static uint16_t transition_buffers_[2][pixoo::now_playing::kArtworkPixelCount];
  static_assert(pixoo::now_playing::kArtworkRgb565Bytes == 8192,
                "one RGB565 transition buffer must be 8 KiB");
  static_assert(sizeof(transition_buffers_) == 16384,
                "the renderer must own exactly two transition buffers");
};

}  // namespace esphome::pixoo64::dashboard
