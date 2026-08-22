#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "artwork_fetch_policy.h"
#include "esphome/core/automation.h"
#include "esphome/core/component.h"
#include "esphome/core/preferences.h"
#include "metadata_policy.h"
#include "now_playing_config.h"
#include "now_playing_source.h"
#include "snapshot_buffer.h"

namespace esphome::api {
class APIServer;
}
namespace esphome::time {
class RealTimeClock;
}
namespace esphome::http_request {
class HttpRequestComponent;
}

namespace esphome::pixoo64::adapters {

class HttpRequestGate;

class HomeAssistantMediaSource final
    : public Component,
      public pixoo::now_playing::NowPlayingSource {
public:
  HomeAssistantMediaSource();
  ~HomeAssistantMediaSource() override;
  void setup() override;
  void loop() override;
  void on_shutdown() override;
  bool teardown() override;
  float get_setup_priority() const override {
    return setup_priority::BEFORE_CONNECTION + 1.0f;
  }

  void set_api_server(api::APIServer *api_server) {
    this->api_server_ = api_server;
  }
  void set_clock(time::RealTimeClock *clock) { this->clock_ = clock; }
  void set_http_request(http_request::HttpRequestComponent *http) {
    this->http_ = http;
  }
  void set_http_request_gate(HttpRequestGate *gate) { this->http_gate_ = gate; }

  bool Configure(const std::string &entity_id,
                 const std::string &home_assistant_url);
  bool Clear();

  pixoo::now_playing::NowPlayingData Data() const override;
  bool SnapshotSettled() const override;
  void SetArtworkEligible(bool eligible, uint32_t now_ms) override;
  bool CopyArtwork(uint64_t expected_identity, uint32_t expected_revision,
                   uint16_t *destination,
                   size_t destination_count) const override;

protected:
  enum InitialField : uint8_t {
    kContentId = 1u << 0,
    kTitle = 1u << 1,
    kArtist = 1u << 2,
    kDuration = 1u << 3,
    kPosition = 1u << 4,
    kPositionUpdatedAt = 1u << 5,
    kEntityPicture = 1u << 6,
  };
  static constexpr uint8_t kAllInitialFields = (1u << 7) - 1;

  void RegisterSubscriptions_();
  void BeginInitialSnapshot_();
  void CompleteInitialSnapshot_();
  void MarkInitialField_(InitialField field);
  void SetTransportConnected_(bool connected, uint32_t now_ms);
  void ResetState_(uint32_t now_ms);
  void Publish_(const pixoo::now_playing::NowPlayingData &data);
  void OnSubscriptionRoot_(StringRef value);
  void OnRoot_(StringRef value);
  void OnRootState_(pixoo::now_playing::PlaybackState state, uint32_t now_ms);
  void OnContentId_(StringRef value);
  void OnTitle_(StringRef value);
  void OnArtist_(StringRef value);
  void OnDuration_(StringRef value);
  void OnPosition_(StringRef value);
  void OnPositionUpdatedAt_(StringRef value);
  void OnEntityPicture_(StringRef value);
  void StagePosition_(double seconds, uint32_t receipt_ms);
  void ClearPositionTracking_();
  bool ObserveContentId_(const char *value, size_t size);
  bool SaveAndSync_(const now_playing_config::ConfigRecord &record);
  bool PersistAtomically_(const now_playing_config::ConfigRecord &next);
  void ClearDesiredArtwork_();
  void ObserveArtworkCompletion_(uint32_t now_ms);
  void ScheduleArtwork_(uint32_t now_ms);
  void FetchArtworkJob_();
  void DecodeArtworkJob_();
  bool FetchArtwork_(const char *url, size_t url_size, uint32_t generation,
                     uint8_t **encoded, size_t *encoded_size);

  api::APIServer *api_server_{nullptr};
  time::RealTimeClock *clock_{nullptr};
  http_request::HttpRequestComponent *http_{nullptr};
  HttpRequestGate *http_gate_{nullptr};
  ESPPreferenceObject preference_{};
  now_playing_config::ConfigRecord record_{};
  pixoo::now_playing::NowPlayingMetadataPolicy policy_{};
  async::SnapshotBuffer<pixoo::now_playing::NowPlayingData> snapshot_{};
  char desired_artwork_url_[now_playing_config::kMaxResolvedArtworkUrlBytes +
                            1]{};
  uint16_t desired_artwork_url_size_{0};
  double last_position_seconds_{0.0};
  uint32_t last_position_received_ms_{0};
  int64_t position_timestamp_ms_utc_{0};
  char last_content_id_
      [pixoo::now_playing::NowPlayingMetadataPolicy::kMaxContentIdBytes + 1]{};
  uint16_t last_content_id_size_{0};
  bool has_last_position_{false};
  bool has_position_timestamp_{false};
  bool content_id_observed_{false};
  bool has_explicit_content_id_{false};
  bool raw_position_pending_content_id_{false};
  bool position_context_allowed_{false};
  bool configured_{false};
  bool subscriptions_registered_{false};
  bool transport_connected_{false};
  bool connected_grace_active_{false};
  bool initial_snapshot_started_{false};
  bool initial_snapshot_complete_{false};
  bool initial_root_seen_{false};
  uint8_t initial_fields_seen_{0};
  pixoo::now_playing::PlaybackState initial_root_state_{
      pixoo::now_playing::PlaybackState::kUnknown};
  uint32_t connected_since_ms_{0};
  uint32_t config_revision_{0};
  uint64_t desired_artwork_identity_{0};
  artwork::FetchPolicy artwork_fetch_policy_{};

  struct Impl;
  Impl *impl_{nullptr};
};

template <typename... Ts>
class NowPlayingConfigureAction final : public Action<Ts...> {
public:
  explicit NowPlayingConfigureAction(HomeAssistantMediaSource *parent)
      : parent_(parent) {}
  TEMPLATABLE_VALUE(std::string, entity_id)
  TEMPLATABLE_VALUE(std::string, home_assistant_url)
  void play(const Ts &...x) override {
    this->parent_->Configure(this->entity_id_.value(x...),
                             this->home_assistant_url_.value(x...));
  }

private:
  HomeAssistantMediaSource *parent_;
};

template <typename... Ts>
class NowPlayingClearAction final : public Action<Ts...> {
public:
  explicit NowPlayingClearAction(HomeAssistantMediaSource *parent)
      : parent_(parent) {}
  void play(const Ts &...x) override {
    (void)sizeof...(x);
    this->parent_->Clear();
  }

private:
  HomeAssistantMediaSource *parent_;
};

} // namespace esphome::pixoo64::adapters
