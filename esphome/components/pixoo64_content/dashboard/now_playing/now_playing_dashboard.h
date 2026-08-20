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
  };

  struct BufferInfo {
    uint64_t visual_key{0};
    uint64_t artwork_identity{0};
    uint32_t artwork_revision{0};
    BufferKind kind{BufferKind::kPlaceholder};
  };

  static constexpr uint32_t kIdentityTransitionMs = 600;
  static constexpr uint32_t kArtworkTransitionMs = 700;
  static constexpr uint32_t kDimTransitionMs = 400;
  static constexpr uint32_t kMarqueePauseMs = 900;
  static constexpr uint32_t kMarqueeStepMs = 80;
  static constexpr int kMarqueeGapPx = 12;
  static constexpr int kTextLeft = 2;
  static constexpr int kTextRight = 62;
  static constexpr int kTextWidth = kTextRight - kTextLeft;

  static uint64_t VisualKey_(const pixoo::now_playing::NowPlayingData &data);
  static bool ReadyArtwork_(
      const pixoo::now_playing::NowPlayingData &data,
      uint64_t identity, uint32_t revision);
  static uint8_t DesiredDim_(
      const pixoo::now_playing::NowPlayingData &data);
  static uint8_t Red_(uint16_t pixel);
  static uint8_t Green_(uint16_t pixel);
  static uint8_t Blue_(uint16_t pixel);

  void InitializeVisual_(uint64_t visual_key, uint32_t now_ms);
  void StartIdentityTransition_(uint64_t visual_key, uint32_t now_ms);
  bool StartArtworkTransition_(
      const pixoo::now_playing::NowPlayingData &data, uint64_t visual_key,
      uint32_t now_ms);
  void CancelTransition_();
  void AdvanceTransition_(uint32_t now_ms);
  void AdvanceDim_(uint32_t now_ms);
  int Measure_(const char *text) const;
  void UpdateTextLayout_(uint32_t now_ms);
  void DrawBackground_(display::Display &display) const;
  void DrawRows_(display::Display &display) const;
  void DrawRow_(display::Display &display, const char *text, int width,
                int offset, int y, Color color) const;
  void DrawStatusMessage_(display::Display &display) const;
  void DrawMarks_(display::Display &display) const;
  void DrawProgress_(display::Display &display) const;

  pixoo::now_playing::NowPlayingSource *source_{nullptr};
  font::Font *font_{nullptr};
  pixoo::now_playing::NowPlayingData snapshot_{};
  pixoo::now_playing::NowPlayingData displayed_{};
  pixoo::now_playing::MarqueeTiming title_marquee_{};
  pixoo::now_playing::MarqueeTiming artist_marquee_{};
  pixoo::now_playing::TransitionTimeline visual_timeline_{};
  pixoo::now_playing::TransitionTimeline dim_timeline_{};
  BufferInfo buffer_info_[2]{};
  uint32_t current_ms_{0};
  int title_width_{0};
  int artist_width_{0};
  int title_offset_{0};
  int artist_offset_{0};
  int8_t front_buffer_{0};
  int8_t transition_buffer_{1};
  TransitionKind transition_kind_{TransitionKind::kNone};
  uint8_t crossfade_{0};
  uint8_t dim_from_{255};
  uint8_t dim_value_{255};
  uint8_t dim_target_{255};
  bool initialized_{false};
  bool dim_transitioning_{false};

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
