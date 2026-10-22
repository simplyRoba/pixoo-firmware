#pragma once

#include "now_playing_data.h"
#include "now_playing_timing.h"

namespace pixoo::now_playing {

struct ArtworkAverage {
  uint8_t red{0};
  uint8_t green{0};
  uint8_t blue{0};
};

// Synchronous access to the renderer's two retained artwork slots.
class NowPlayingPresentationArtwork {
 public:
  virtual ~NowPlayingPresentationArtwork() = default;
  virtual ArtworkAverage PreparePlaceholder(int8_t slot, uint64_t key) = 0;
  virtual bool CopyArtwork(int8_t slot, uint64_t identity, uint32_t revision,
                           ArtworkAverage &average) = 0;
  virtual bool EqualArtwork(int8_t left, int8_t right) const = 0;
};

class NowPlayingPresentation {
 public:
  enum class BufferKind : uint8_t { kPlaceholder, kArtwork };
  enum class TransitionKind : uint8_t {
    kNone, kIdentityPlaceholder, kArtwork, kText,
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

  void Update(const NowPlayingData &snapshot, uint32_t now_ms,
              NowPlayingPresentationArtwork &artwork);
  void UpdateTextLayout(int title_width, int artist_width, uint32_t now_ms);
  void OnShow();
  void OnHide() { hidden_ = true; }
  static bool ReadyToShow(const NowPlayingData &data, bool settled);
  static bool ShowsMetadata(const NowPlayingData &data);

  const NowPlayingData &Displayed() const { return displayed_; }
  const BufferInfo &Front() const { return buffer_info_[front_buffer_]; }
  const BufferInfo &Target() const { return buffer_info_[transition_buffer_]; }
  int8_t FrontIndex() const { return front_buffer_; }
  int8_t TargetIndex() const { return transition_buffer_; }
  TransitionKind Kind() const { return transition_kind_; }
  uint8_t Crossfade() const { return crossfade_; }
  uint8_t TextOpacity() const { return text_opacity_; }
  uint8_t Dim() const { return dim_value_; }
  int TitleWidth() const { return title_width_; }
  int ArtistWidth() const { return artist_width_; }
  int TitleOffset() const { return title_offset_; }
  int ArtistOffset() const { return artist_offset_; }
  bool Initialized() const { return initialized_; }

 private:
  static constexpr uint32_t kIdentityTransitionMs = 600;
  static constexpr uint32_t kArtworkTransitionMs = 700;
  static constexpr uint32_t kDimTransitionMs = 400;
  static constexpr uint32_t kMarqueePauseMs = 900;
  static constexpr uint32_t kMarqueeStepMs = 80;
  static constexpr int kMarqueeGapPx = 12;
  static constexpr int kTextWidth = 60;
  static uint64_t VisualKey_(const NowPlayingData &data);
  static uint8_t DesiredDim_(const NowPlayingData &data);
  void ResetPresentation_();
  void InitializeVisual_(uint64_t visual_key, NowPlayingPresentationArtwork &artwork);
  void StartIdentityTransition_(const NowPlayingData &data, uint64_t visual_key,
                                 uint32_t now_ms, NowPlayingPresentationArtwork &artwork);
  bool StartArtworkTransition_(const NowPlayingData &data, uint64_t visual_key,
                                uint32_t now_ms, NowPlayingPresentationArtwork &artwork);
  void StartTextTransition_(const NowPlayingData &data, uint32_t now_ms);
  void StageMetadata_(const NowPlayingData &data);
  bool TransitionMatches_(const NowPlayingData &data) const;
  void CancelTransition_();
  void AdvanceTransition_(uint32_t now_ms);
  void AdvanceDim_(uint32_t now_ms);
  void SetAverage_(int8_t slot, ArtworkAverage average);

  NowPlayingData snapshot_{};
  NowPlayingData displayed_{};
  NowPlayingData transition_from_data_{};
  NowPlayingData transition_data_{};
  MarqueeTiming title_marquee_{};
  MarqueeTiming artist_marquee_{};
  TransitionTimeline visual_timeline_{};
  TransitionTimeline dim_timeline_{};
  BufferInfo buffer_info_[2]{};
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
};

}  // namespace pixoo::now_playing
