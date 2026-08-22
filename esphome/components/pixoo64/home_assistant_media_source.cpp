#include "home_assistant_media_source.h"

#ifdef USE_PIXOO64_NOW_PLAYING

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <limits>

#include "esp_heap_caps.h"

#include "async_worker.h"
#include "esphome/components/api/api_server.h"
#include "esphome/components/http_request/http_request.h"
#include "esphome/components/time/real_time_clock.h"
#include "esphome/core/application.h"
#include "esphome/core/hal.h"
#include "esphome/core/log.h"
#include "http_body_reader.h"
#include "http_request_gate.h"
#include "snapshot_buffer.h"

namespace esphome::pixoo64::adapters {
namespace {
const char *const TAG = "pixoo64.now_playing";
constexpr uint32_t kInitialConnectedGraceMs = 5000;
constexpr int64_t kTimestampFutureToleranceSeconds = 5;
constexpr int64_t kTimestampOldLimitSeconds = 7 * 24 * 60 * 60;
constexpr uint64_t kArtworkIdentityDomain = 0x415254574f524b55ull; // ARTWORKU
constexpr size_t kArtworkReadChunkBytes = 512;

uint16_t *AllocateArtworkSlot() {
  // Source slots are copied only when an artwork revision changes. They are
  // deliberately PSRAM-only; consuming internal render memory as a fallback is
  // worse than degrading to the procedural artwork path.
  return static_cast<uint16_t *>(heap_caps_malloc(
      artwork::kArtworkRgb565Bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
}

struct ArtworkCancellationContext {
  const async::AsyncWorker *worker{nullptr};
  const std::atomic<bool> *eligible{nullptr};
  const std::atomic<uint32_t> *generation{nullptr};
  uint32_t expected_generation{0};
};

bool ArtworkCancelled(void *opaque) {
  auto *context = static_cast<ArtworkCancellationContext *>(opaque);
  return context == nullptr || context->worker == nullptr ||
         context->eligible == nullptr || context->generation == nullptr ||
         context->worker->StopRequested() ||
         !context->eligible->load(std::memory_order_acquire) ||
         context->generation->load(std::memory_order_acquire) !=
             context->expected_generation;
}

// Current ESPHome transport logs its input URL on non-2xx status. Production
// enables per-tag runtime levels and keeps this transport tag disabled so a
// signed artwork query cannot reach UART or native-API logs. Project-owned
// warnings contain only status and byte counts.

bool ParseFiniteDecimal(StringRef value, double *output) {
  if (output == nullptr || value.empty() || value.size() >= 64)
    return false;
  size_t offset = 0;
  bool has_digit = false;
  while (offset < value.size() && value[offset] >= '0' &&
         value[offset] <= '9') {
    has_digit = true;
    ++offset;
  }
  if (offset < value.size() && value[offset] == '.') {
    ++offset;
    while (offset < value.size() && value[offset] >= '0' &&
           value[offset] <= '9') {
      has_digit = true;
      ++offset;
    }
  }
  if (!has_digit)
    return false;
  if (offset < value.size() && (value[offset] == 'e' || value[offset] == 'E')) {
    ++offset;
    if (offset < value.size() && (value[offset] == '+' || value[offset] == '-'))
      ++offset;
    const size_t exponent_start = offset;
    while (offset < value.size() && value[offset] >= '0' &&
           value[offset] <= '9')
      ++offset;
    if (offset == exponent_start)
      return false;
  }
  if (offset != value.size())
    return false;
  char buffer[64];
  std::memcpy(buffer, value.c_str(), value.size());
  buffer[value.size()] = '\0';
  char *end = nullptr;
  const double parsed = std::strtod(buffer, &end);
  if (end != buffer + value.size() || !std::isfinite(parsed) || parsed < 0.0)
    return false;
  *output = parsed;
  return true;
}

bool IsLeapYear(int year) {
  return (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
}

bool ParseDigits(const char *value, size_t offset, size_t count, int *result) {
  int parsed = 0;
  for (size_t i = 0; i < count; ++i) {
    const char c = value[offset + i];
    if (c < '0' || c > '9')
      return false;
    parsed = parsed * 10 + c - '0';
  }
  *result = parsed;
  return true;
}

int64_t DaysFromCivil(int year, unsigned month, unsigned day) {
  year -= month <= 2;
  const int era = (year >= 0 ? year : year - 399) / 400;
  const unsigned yoe = static_cast<unsigned>(year - era * 400);
  const unsigned doy = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + static_cast<int>(doe) - 719468;
}

bool ParseHaUtcTimestampMs(StringRef value, int64_t *epoch_ms) {
  if (epoch_ms == nullptr || value.size() < 20 || value.size() >= 64)
    return false;
  char buffer[64];
  std::memcpy(buffer, value.c_str(), value.size());
  buffer[value.size()] = '\0';
  int year, month, day, hour, minute, second;
  if (!ParseDigits(buffer, 0, 4, &year) || buffer[4] != '-' ||
      !ParseDigits(buffer, 5, 2, &month) || buffer[7] != '-' ||
      !ParseDigits(buffer, 8, 2, &day) || buffer[10] != 'T' ||
      !ParseDigits(buffer, 11, 2, &hour) || buffer[13] != ':' ||
      !ParseDigits(buffer, 14, 2, &minute) || buffer[16] != ':' ||
      !ParseDigits(buffer, 17, 2, &second))
    return false;
  static constexpr unsigned kDays[] = {31, 28, 31, 30, 31, 30,
                                       31, 31, 30, 31, 30, 31};
  if (year < 1970 || month < 1 || month > 12 || day < 1 ||
      day > static_cast<int>(kDays[month - 1] +
                             (month == 2 && IsLeapYear(year))) ||
      hour > 23 || minute > 59 || second > 59)
    return false;
  size_t offset = 19;
  int milliseconds = 0;
  if (offset < value.size() && buffer[offset] == '.') {
    const size_t first_fraction = ++offset;
    size_t fraction_digits = 0;
    while (offset < value.size() && buffer[offset] >= '0' &&
           buffer[offset] <= '9') {
      if (fraction_digits < 3)
        milliseconds = milliseconds * 10 + buffer[offset] - '0';
      ++fraction_digits;
      ++offset;
    }
    if (offset == first_fraction)
      return false;
    while (fraction_digits < 3) {
      milliseconds *= 10;
      ++fraction_digits;
    }
  }
  if (offset >= value.size())
    return false;
  if (buffer[offset] == 'Z') {
    if (++offset != value.size())
      return false;
  } else if (buffer[offset] == '+' || buffer[offset] == '-') {
    if (offset + 6 != value.size() || buffer[offset + 1] != '0' ||
        buffer[offset + 2] != '0' || buffer[offset + 3] != ':' ||
        buffer[offset + 4] != '0' || buffer[offset + 5] != '0')
      return false;
  } else {
    return false;
  }
  *epoch_ms = (DaysFromCivil(year, static_cast<unsigned>(month),
                             static_cast<unsigned>(day)) *
                   86400 +
               hour * 3600 + minute * 60 + second) *
                  1000 +
              milliseconds;
  return true;
}

} // namespace

struct HomeAssistantMediaSource::Impl {
  struct Request {
    char url[now_playing_config::kMaxResolvedArtworkUrlBytes + 1]{};
    uint16_t url_size{0};
    uint64_t identity{0};
    uint32_t revision{0};
    uint32_t generation{0};
  };
  struct Completion {
    Request request{};
    bool succeeded{false};
  };

  explicit Impl(std::function<void()> fetch_job,
                std::function<void()> decode_job)
      : fetch_worker(std::move(fetch_job), 12288, 0, 0, false,
                     "pixoo_art_http"),
        decode_worker(std::move(decode_job), 12288, 0, 1, false,
                      "pixoo_art_dec") {}
  ~Impl() {
    this->ReclaimHandoff();
    if (this->published_encoded != nullptr)
      heap_caps_free(this->published_encoded);
  }

  void ReclaimHandoff() {
    uint8_t *body = nullptr;
    taskENTER_CRITICAL(&this->artwork_mux);
    body = this->handoff_encoded;
    this->handoff_encoded = nullptr;
    this->handoff_encoded_size = 0;
    this->decode_pending = false;
    taskEXIT_CRITICAL(&this->artwork_mux);
    if (body != nullptr)
      heap_caps_free(body);
  }

  async::AsyncWorker fetch_worker;
  async::AsyncWorker decode_worker;
  async::SnapshotBuffer<Request> request;
  async::SnapshotBuffer<Completion> completion;
  std::atomic<bool> completion_ready{false};
  std::atomic<bool> in_flight{false};
  std::atomic<bool> eligible{false};
  std::atomic<uint32_t> generation{1};
  Request handoff_request{};
  uint8_t *handoff_encoded{nullptr};
  size_t handoff_encoded_size{0};
  bool decode_pending{false};
  mutable portMUX_TYPE artwork_mux = portMUX_INITIALIZER_UNLOCKED;
  uint16_t *slots[artwork::kArtworkSlotCount]{nullptr, nullptr};
  uint32_t reader_pins[artwork::kArtworkSlotCount]{0, 0};
  int8_t published_slot{-1};
  int8_t writing_slot{-1};
  uint64_t published_identity{0};
  uint64_t writing_identity{0};
  // This allocation is the exact validated body that produced published_slot.
  // It is owned by that slot's publication, never copied into another cache.
  uint8_t *published_encoded{nullptr};
  size_t published_encoded_size{0};
  int8_t published_encoded_slot{-1};
  uint32_t published_revision{0};
  uint32_t writing_revision{0};
  uint32_t writing_generation{0};
};

HomeAssistantMediaSource::HomeAssistantMediaSource() {
  this->impl_ = new Impl([this]() { this->FetchArtworkJob_(); },
                         [this]() { this->DecodeArtworkJob_(); });
}

HomeAssistantMediaSource::~HomeAssistantMediaSource() {
  if (this->impl_ != nullptr) {
    this->impl_->fetch_worker.RequestStop();
    this->impl_->decode_worker.RequestStop();
    this->impl_->fetch_worker.Stop();
    this->impl_->decode_worker.Stop();
    this->impl_->ReclaimHandoff();
    if (this->impl_->slots[0] != nullptr)
      heap_caps_free(this->impl_->slots[0]);
    if (this->impl_->slots[1] != nullptr)
      heap_caps_free(this->impl_->slots[1]);
    delete this->impl_;
    this->impl_ = nullptr;
  }
}

void HomeAssistantMediaSource::setup() {
  if (this->api_server_ == nullptr) {
    ESP_LOGE(TAG, "API server is required");
    this->mark_failed();
    return;
  }
  if (global_preferences != nullptr) {
    this->preference_ =
        global_preferences->make_preference<now_playing_config::ConfigRecord>(
            now_playing_config::kPreferenceKey, true);
    now_playing_config::ConfigRecord loaded{};
    if (this->preference_.load(&loaded) &&
        now_playing_config::ValidateConfigRecord(loaded)) {
      this->record_ = loaded;
      this->configured_ = true;
      this->config_revision_ = loaded.revision;
    }
  }
  this->transport_connected_ =
      this->api_server_->is_connected_with_state_subscription();
  this->ResetState_(millis());
  if (this->configured_)
    this->RegisterSubscriptions_();
  if (this->impl_ != nullptr && this->configured_) {
    this->impl_->slots[0] = AllocateArtworkSlot();
    this->impl_->slots[1] = AllocateArtworkSlot();
    if (this->impl_->slots[0] == nullptr || this->impl_->slots[1] == nullptr) {
      if (this->impl_->slots[0] != nullptr)
        heap_caps_free(this->impl_->slots[0]);
      if (this->impl_->slots[1] != nullptr)
        heap_caps_free(this->impl_->slots[1]);
      this->impl_->slots[0] = nullptr;
      this->impl_->slots[1] = nullptr;
      ESP_LOGW(TAG,
               "PSRAM artwork allocation failed; metadata remains available");
    } else if (!this->impl_->fetch_worker.Start() ||
               !this->impl_->decode_worker.Start()) {
      this->impl_->fetch_worker.RequestStop();
      this->impl_->decode_worker.RequestStop();
      this->impl_->fetch_worker.Stop();
      this->impl_->decode_worker.Stop();
      heap_caps_free(this->impl_->slots[0]);
      heap_caps_free(this->impl_->slots[1]);
      this->impl_->slots[0] = nullptr;
      this->impl_->slots[1] = nullptr;
      ESP_LOGE(TAG, "failed to start PSRAM artwork workers");
    } else {
      ESP_LOGCONFIG(TAG,
                    "Artwork HTTP task: task=pixoo_art_http core=0 priority=0 "
                    "stack=12288 bytes in PSRAM");
      ESP_LOGCONFIG(TAG,
                    "Artwork decode task: task=pixoo_art_dec core=1 priority=0 "
                    "stack=12288 bytes in PSRAM");
    }
  }
}

void HomeAssistantMediaSource::on_shutdown() {
  if (this->impl_ != nullptr) {
    this->impl_->fetch_worker.RequestStop();
    this->impl_->decode_worker.RequestStop();
    this->impl_->in_flight.store(false, std::memory_order_release);
  }
}

bool HomeAssistantMediaSource::teardown() {
  if (this->impl_ == nullptr)
    return true;
  if (!this->impl_->fetch_worker.IsStopped() ||
      !this->impl_->decode_worker.IsStopped())
    return false;
  this->impl_->ReclaimHandoff();
  return true;
}

void HomeAssistantMediaSource::ClearDesiredArtwork_() {
  std::memset(this->desired_artwork_url_, 0,
              sizeof(this->desired_artwork_url_));
  this->desired_artwork_url_size_ = 0;
  this->desired_artwork_identity_ = 0;
  this->artwork_fetch_policy_.SetDesired(0);
  if (this->impl_ != nullptr) {
    taskENTER_CRITICAL(&this->impl_->artwork_mux);
    this->impl_->generation.store(this->artwork_fetch_policy_.generation(),
                                  std::memory_order_release);
    taskEXIT_CRITICAL(&this->impl_->artwork_mux);
  }
}

void HomeAssistantMediaSource::ResetState_(uint32_t now_ms) {
  this->initial_snapshot_started_ = false;
  this->initial_snapshot_complete_ = false;
  this->initial_root_seen_ = false;
  this->initial_root_state_ = pixoo::now_playing::PlaybackState::kUnknown;
  this->initial_fields_seen_ = 0;
  this->ClearDesiredArtwork_();
  this->ClearPositionTracking_();
  this->content_id_observed_ = false;
  this->has_explicit_content_id_ = false;
  this->last_content_id_size_ = 0;
  this->last_content_id_[0] = '\0';
  this->position_context_allowed_ =
      this->configured_ && this->transport_connected_;
  this->connected_grace_active_ =
      this->configured_ && this->transport_connected_;
  this->connected_since_ms_ = now_ms;
  pixoo::now_playing::NowPlayingData data;
  this->policy_.Reset(this->configured_, this->config_revision_,
                      this->transport_connected_, now_ms, &data);
  this->Publish_(data);
}

void HomeAssistantMediaSource::RegisterSubscriptions_() {
  if (this->subscriptions_registered_ || !this->configured_)
    return;
  const char *entity = this->record_.entity_id;
  this->api_server_->get_home_assistant_state(
      entity, "friendly_name",
      [this](StringRef) { this->BeginInitialSnapshot_(); });
  this->api_server_->subscribe_home_assistant_state(
      entity, nullptr,
      [this](StringRef value) { this->OnSubscriptionRoot_(value); });
  this->api_server_->subscribe_home_assistant_state(
      entity, "media_content_id", [this](StringRef value) {
        this->MarkInitialField_(kContentId);
        this->OnContentId_(value);
      });
  this->api_server_->subscribe_home_assistant_state(
      entity, "media_title", [this](StringRef value) {
        this->MarkInitialField_(kTitle);
        this->OnTitle_(value);
      });
  this->api_server_->subscribe_home_assistant_state(
      entity, "media_artist", [this](StringRef value) {
        this->MarkInitialField_(kArtist);
        this->OnArtist_(value);
      });
  this->api_server_->subscribe_home_assistant_state(
      entity, "media_duration", [this](StringRef value) {
        this->MarkInitialField_(kDuration);
        this->OnDuration_(value);
      });
  this->api_server_->subscribe_home_assistant_state(
      entity, "media_position", [this](StringRef value) {
        this->MarkInitialField_(kPosition);
        this->OnPosition_(value);
      });
  this->api_server_->subscribe_home_assistant_state(
      entity, "media_position_updated_at", [this](StringRef value) {
        this->MarkInitialField_(kPositionUpdatedAt);
        this->OnPositionUpdatedAt_(value);
      });
  this->api_server_->subscribe_home_assistant_state(
      entity, "entity_picture", [this](StringRef value) {
        this->MarkInitialField_(kEntityPicture);
        this->OnEntityPicture_(value);
      });
  this->api_server_->get_home_assistant_state(
      entity, "supported_features",
      [this](StringRef) { this->CompleteInitialSnapshot_(); });
  this->subscriptions_registered_ = true;
}

void HomeAssistantMediaSource::BeginInitialSnapshot_() {
  if (!this->configured_)
    return;
  const uint32_t now_ms = millis();
  this->SetTransportConnected_(true, now_ms);
  this->initial_snapshot_started_ = true;
  this->initial_snapshot_complete_ = false;
  this->initial_root_seen_ = false;
  this->initial_root_state_ = pixoo::now_playing::PlaybackState::kUnknown;
  this->initial_fields_seen_ = 0;
}

void HomeAssistantMediaSource::MarkInitialField_(InitialField field) {
  if (this->initial_snapshot_started_ && !this->initial_snapshot_complete_)
    this->initial_fields_seen_ |= static_cast<uint8_t>(field);
}

void HomeAssistantMediaSource::CompleteInitialSnapshot_() {
  if (!this->configured_ || !this->initial_snapshot_started_ ||
      !this->initial_root_seen_)
    return;
  this->OnRootState_(this->initial_root_state_, millis());
  if ((this->initial_fields_seen_ & kContentId) == 0)
    this->OnContentId_({});
  if ((this->initial_fields_seen_ & kTitle) == 0)
    this->OnTitle_({});
  if ((this->initial_fields_seen_ & kArtist) == 0)
    this->OnArtist_({});
  if ((this->initial_fields_seen_ & kDuration) == 0)
    this->OnDuration_({});
  if ((this->initial_fields_seen_ & kPosition) == 0)
    this->OnPosition_({});
  if ((this->initial_fields_seen_ & kPositionUpdatedAt) == 0)
    this->OnPositionUpdatedAt_({});
  if ((this->initial_fields_seen_ & kEntityPicture) == 0)
    this->OnEntityPicture_({});
  pixoo::now_playing::NowPlayingData data;
  if (this->policy_.ForcePublish(millis(), &data))
    this->Publish_(data);
  this->initial_fields_seen_ = kAllInitialFields;
  this->initial_snapshot_complete_ = true;
  this->initial_snapshot_started_ = false;
}

void HomeAssistantMediaSource::SetTransportConnected_(bool connected,
                                                       uint32_t now_ms) {
  if (connected == this->transport_connected_)
    return;
  this->transport_connected_ = connected;
  pixoo::now_playing::NowPlayingData data;
  if (this->policy_.SetTransportConnected(connected, now_ms, &data))
    this->Publish_(data);
  if (connected) {
    this->position_context_allowed_ = this->configured_;
    this->artwork_fetch_policy_.ResetRetry();
  } else {
    this->position_context_allowed_ = false;
    this->ClearPositionTracking_();
    this->initial_snapshot_started_ = false;
    this->initial_snapshot_complete_ = false;
    this->initial_root_seen_ = false;
    this->initial_root_state_ = pixoo::now_playing::PlaybackState::kUnknown;
    this->initial_fields_seen_ = 0;
  }
  this->connected_grace_active_ = this->configured_ && connected;
  this->connected_since_ms_ = now_ms;
}

void HomeAssistantMediaSource::loop() {
  const uint32_t now_ms = millis();
  const bool connected =
      this->api_server_ != nullptr &&
      this->api_server_->is_connected_with_state_subscription();
  this->SetTransportConnected_(connected, now_ms);
  if (this->connected_grace_active_ &&
      now_ms - this->connected_since_ms_ >= kInitialConnectedGraceMs) {
    this->connected_grace_active_ = false;
    pixoo::now_playing::NowPlayingData data;
    if (this->policy_.MarkNoEntityData(now_ms, &data))
      this->Publish_(data);
  }
  pixoo::now_playing::NowPlayingData data;
  if (this->policy_.PublishIfDue(now_ms, &data)) {
    this->raw_position_pending_content_id_ = false;
    this->Publish_(data);
  }
  this->ObserveArtworkCompletion_(now_ms);
  this->ScheduleArtwork_(now_ms);
}

void HomeAssistantMediaSource::Publish_(
    const pixoo::now_playing::NowPlayingData &data) {
  this->snapshot_.Publish(data);
}

pixoo::now_playing::NowPlayingData HomeAssistantMediaSource::Data() const {
  return this->snapshot_.has_value() ? this->snapshot_.Get()
                                     : this->policy_.Data();
}

bool HomeAssistantMediaSource::SnapshotSettled() const {
  if (!this->configured_)
    return true;
  return this->transport_connected_ && this->initial_snapshot_complete_ &&
         !this->policy_.HasPendingPublication();
}

void HomeAssistantMediaSource::SetArtworkEligible(bool eligible, uint32_t) {
  this->artwork_fetch_policy_.SetVisible(eligible);
  if (this->impl_ != nullptr) {
    // Visibility/generation and slot publication use the same lock. If OnHide
    // obtains it first, the worker's final checkpoint cannot publish; if the
    // worker obtains it first, publication completed while still visible.
    taskENTER_CRITICAL(&this->impl_->artwork_mux);
    this->impl_->eligible.store(eligible, std::memory_order_release);
    this->impl_->generation.store(this->artwork_fetch_policy_.generation(),
                                  std::memory_order_release);
    taskEXIT_CRITICAL(&this->impl_->artwork_mux);
  }
}

bool HomeAssistantMediaSource::CopyArtwork(uint64_t expected_identity,
                                           uint32_t expected_revision,
                                           uint16_t *destination,
                                           size_t destination_count) const {
  if (this->impl_ == nullptr || destination == nullptr ||
      destination_count < artwork::kArtworkPixelCount)
    return false;

  int8_t pinned_slot = -1;
  const uint16_t *source = nullptr;
  taskENTER_CRITICAL(&this->impl_->artwork_mux);
  const int8_t published = this->impl_->published_slot;
  if (published >= 0 &&
      published < static_cast<int8_t>(artwork::kArtworkSlotCount) &&
      this->impl_->slots[published] != nullptr &&
      this->impl_->published_identity == expected_identity &&
      this->impl_->published_revision == expected_revision &&
      this->impl_->reader_pins[published] !=
          std::numeric_limits<uint32_t>::max()) {
    ++this->impl_->reader_pins[published];
    pinned_slot = published;
    source = this->impl_->slots[published];
  }
  taskEXIT_CRITICAL(&this->impl_->artwork_mux);
  if (source == nullptr)
    return false;

  std::memcpy(destination, source, artwork::kArtworkRgb565Bytes);

  taskENTER_CRITICAL(&this->impl_->artwork_mux);
  if (pinned_slot >= 0 &&
      pinned_slot < static_cast<int8_t>(artwork::kArtworkSlotCount) &&
      this->impl_->reader_pins[pinned_slot] != 0)
    --this->impl_->reader_pins[pinned_slot];
  taskEXIT_CRITICAL(&this->impl_->artwork_mux);
  return true;
}

void HomeAssistantMediaSource::ObserveArtworkCompletion_(uint32_t now_ms) {
  if (this->impl_ == nullptr ||
      !this->impl_->completion_ready.exchange(false, std::memory_order_acq_rel))
    return;
  const Impl::Completion completion = this->impl_->completion.Get();
  if (!this->artwork_fetch_policy_.Accepts(completion.request.generation,
                                           completion.request.identity))
    return;

  pixoo::now_playing::NowPlayingData data;
  if (this->policy_.CompleteArtwork(completion.request.identity,
                                    completion.request.revision,
                                    completion.succeeded, now_ms, &data)) {
    if (completion.succeeded)
      this->artwork_fetch_policy_.Succeeded();
    else
      this->artwork_fetch_policy_.Failed(now_ms);
    this->Publish_(data);
  } else if (!completion.succeeded) {
    this->artwork_fetch_policy_.Failed(now_ms);
  }
}

void HomeAssistantMediaSource::ScheduleArtwork_(uint32_t now_ms) {
  if (this->impl_ == nullptr || this->impl_->fetch_worker.StopRequested() ||
      this->impl_->decode_worker.StopRequested() ||
      this->desired_artwork_url_size_ == 0 || this->http_ == nullptr ||
      this->http_gate_ == nullptr)
    return;
  auto data = this->Data();
  if (!data.has_artwork_identity ||
      data.artwork_identity != this->desired_artwork_identity_)
    return;

  bool complete = false;
  bool slots_available = false;
  taskENTER_CRITICAL(&this->impl_->artwork_mux);
  slots_available =
      this->impl_->slots[0] != nullptr && this->impl_->slots[1] != nullptr;
  complete = this->impl_->published_slot >= 0 &&
             this->impl_->published_identity == data.artwork_identity &&
             this->impl_->published_revision == data.artwork_revision;
  taskEXIT_CRITICAL(&this->impl_->artwork_mux);

  // A complete slot can outlive a hide/show boundary even if its completion
  // handoff was discarded while hidden. Reattach it without another fetch.
  if (complete) {
    if (data.artwork_availability ==
        pixoo::now_playing::ArtworkAvailability::kPending) {
      pixoo::now_playing::NowPlayingData ready;
      if (this->policy_.CompleteArtwork(data.artwork_identity,
                                        data.artwork_revision, true, now_ms,
                                        &ready)) {
        this->artwork_fetch_policy_.Succeeded();
        this->Publish_(ready);
      }
    }
    return;
  }

  if (!slots_available) {
    if (this->impl_->eligible.load(std::memory_order_acquire) &&
        data.artwork_availability ==
            pixoo::now_playing::ArtworkAvailability::kPending) {
      pixoo::now_playing::NowPlayingData failed;
      if (this->policy_.CompleteArtwork(data.artwork_identity,
                                        data.artwork_revision, false, now_ms,
                                        &failed))
        this->Publish_(failed);
      // Slot allocation cannot recover without reboot, so keep metadata/no-art
      // usable without downloading bodies that cannot be published.
      this->artwork_fetch_policy_.Succeeded();
    }
    return;
  }

  // CompleteArtwork(false) marks the visible fallback without changing the
  // identity. A due retry reopens that same revision as pending.
  if (data.artwork_availability ==
          pixoo::now_playing::ArtworkAvailability::kFailed &&
      this->artwork_fetch_policy_.ShouldStart(
          now_ms, false,
          this->impl_->in_flight.load(std::memory_order_acquire)) &&
      !this->impl_->completion_ready.load(std::memory_order_acquire)) {
    pixoo::now_playing::NowPlayingData reopened;
    if (this->policy_.BeginArtworkRetry(
            data.artwork_identity, data.artwork_revision, now_ms, &reopened)) {
      this->Publish_(reopened);
      data = reopened;
    }
  }
  if (this->impl_->completion_ready.load(std::memory_order_acquire) ||
      !this->artwork_fetch_policy_.ShouldStart(
          now_ms, false,
          this->impl_->in_flight.load(std::memory_order_acquire)))
    return;

  Impl::Request request{};
  request.url_size = this->desired_artwork_url_size_;
  std::memcpy(request.url, this->desired_artwork_url_, request.url_size + 1);
  request.identity = data.artwork_identity;
  request.revision = data.artwork_revision;
  request.generation = this->artwork_fetch_policy_.generation();
  this->impl_->request.Publish(request);
  this->impl_->in_flight.store(true, std::memory_order_release);
  if (this->impl_->fetch_worker.Wake())
    return;

  this->impl_->in_flight.store(false, std::memory_order_release);
  ESP_LOGW(TAG, "artwork HTTP task unavailable");
  if (!this->impl_->fetch_worker.StopRequested() &&
      !this->impl_->decode_worker.StopRequested()) {
    Impl::Completion completion{};
    completion.request = request;
    this->impl_->completion.Publish(completion);
    this->impl_->completion_ready.store(true, std::memory_order_release);
  }
}

void HomeAssistantMediaSource::FetchArtworkJob_() {
  if (this->impl_ == nullptr)
    return;
  if (this->impl_->fetch_worker.StopRequested()) {
    this->impl_->in_flight.store(false, std::memory_order_release);
    return;
  }
  const Impl::Request request = this->impl_->request.Get();
  uint8_t *encoded = nullptr;
  size_t encoded_size = 0;
  const bool fetched =
      this->FetchArtwork_(request.url, request.url_size, request.generation,
                          &encoded, &encoded_size);
  ArtworkCancellationContext cancellation{
      &this->impl_->fetch_worker, &this->impl_->eligible,
      &this->impl_->generation, request.generation};
  const auto current = [&cancellation]() {
    return !ArtworkCancelled(&cancellation);
  };
  const auto complete_failure = [&]() {
    if (!this->impl_->fetch_worker.StopRequested() &&
        !this->impl_->decode_worker.StopRequested()) {
      Impl::Completion completion{};
      completion.request = request;
      this->impl_->completion.Publish(completion);
      this->impl_->completion_ready.store(true, std::memory_order_release);
    }
    this->impl_->in_flight.store(false, std::memory_order_release);
  };

  if (!fetched || !current()) {
    if (encoded != nullptr)
      heap_caps_free(encoded);
    complete_failure();
    return;
  }

  bool handed_off = false;
  taskENTER_CRITICAL(&this->impl_->artwork_mux);
  if (current() && !this->impl_->decode_pending &&
      this->impl_->handoff_encoded == nullptr) {
    this->impl_->handoff_request = request;
    this->impl_->handoff_encoded = encoded;
    this->impl_->handoff_encoded_size = encoded_size;
    this->impl_->decode_pending = true;
    handed_off = true;
  }
  taskEXIT_CRITICAL(&this->impl_->artwork_mux);
  if (!handed_off) {
    heap_caps_free(encoded);
    complete_failure();
    return;
  }

  // The handoff transfers the sole encoded-body pointer; decode owns it once
  // it claims the pending record under artwork_mux.
  if (this->impl_->decode_worker.Wake())
    return;

  uint8_t *unclaimed = nullptr;
  taskENTER_CRITICAL(&this->impl_->artwork_mux);
  if (this->impl_->decode_pending && this->impl_->handoff_encoded == encoded) {
    unclaimed = this->impl_->handoff_encoded;
    this->impl_->handoff_encoded = nullptr;
    this->impl_->handoff_encoded_size = 0;
    this->impl_->decode_pending = false;
  }
  taskEXIT_CRITICAL(&this->impl_->artwork_mux);
  if (unclaimed != nullptr)
    heap_caps_free(unclaimed);
  complete_failure();
}

void HomeAssistantMediaSource::DecodeArtworkJob_() {
  if (this->impl_ == nullptr)
    return;
  Impl::Request request{};
  uint8_t *encoded = nullptr;
  size_t encoded_size = 0;
  taskENTER_CRITICAL(&this->impl_->artwork_mux);
  if (this->impl_->decode_pending) {
    request = this->impl_->handoff_request;
    encoded = this->impl_->handoff_encoded;
    encoded_size = this->impl_->handoff_encoded_size;
    this->impl_->handoff_encoded = nullptr;
    this->impl_->handoff_encoded_size = 0;
    this->impl_->decode_pending = false;
  }
  taskEXIT_CRITICAL(&this->impl_->artwork_mux);
  if (encoded == nullptr) {
    this->impl_->in_flight.store(false, std::memory_order_release);
    return;
  }

  ArtworkCancellationContext cancellation{
      &this->impl_->decode_worker, &this->impl_->eligible,
      &this->impl_->generation, request.generation};
  const auto current = [&cancellation]() {
    return !ArtworkCancelled(&cancellation);
  };

  Impl::Completion completion{};
  completion.request = request;
  bool reused = false;
  int8_t target_slot = -1;
  uint16_t *target = nullptr;
  uint8_t *cached_encoded = nullptr;
  size_t cached_encoded_size = 0;
  int8_t cached_slot = -1;
  if (current()) {
    taskENTER_CRITICAL(&this->impl_->artwork_mux);
    const int8_t published = this->impl_->published_slot;
    if (published >= 0 &&
        published < static_cast<int8_t>(artwork::kArtworkSlotCount) &&
        this->impl_->slots[published] != nullptr &&
        this->impl_->published_encoded_slot == published) {
      cached_encoded = this->impl_->published_encoded;
      cached_encoded_size = this->impl_->published_encoded_size;
      cached_slot = published;
    }
    taskEXIT_CRITICAL(&this->impl_->artwork_mux);
  }
  // Only this decode worker replaces published_encoded, and destruction waits
  // for it to stop. Compare the bounded PSRAM bodies without holding the
  // cross-core publication lock.
  const bool body_matches =
      current() && artwork::EncodedBodiesEqual(
                       encoded, encoded_size, cached_encoded,
                       cached_encoded_size);
  if (current()) {
    taskENTER_CRITICAL(&this->impl_->artwork_mux);
    if (body_matches && current() && cached_slot >= 0 &&
        this->impl_->published_slot == cached_slot &&
        this->impl_->published_encoded_slot == cached_slot &&
        this->impl_->published_encoded == cached_encoded &&
        this->impl_->published_encoded_size == cached_encoded_size) {
      // Readers may continue using this slot: its pixels do not change.
      this->impl_->published_identity = request.identity;
      this->impl_->published_revision = request.revision;
      completion.succeeded = true;
      reused = true;
    } else if (current() && this->impl_->writing_slot < 0) {
      target_slot = artwork::SelectWritableSlot(this->impl_->published_slot,
                                                this->impl_->reader_pins);
      if (target_slot >= 0 &&
          target_slot < static_cast<int8_t>(artwork::kArtworkSlotCount) &&
          this->impl_->slots[target_slot] != nullptr) {
        target = this->impl_->slots[target_slot];
        this->impl_->writing_slot = target_slot;
        this->impl_->writing_identity = request.identity;
        this->impl_->writing_revision = request.revision;
        this->impl_->writing_generation = request.generation;
      } else {
        target_slot = -1;
      }
    }
    taskEXIT_CRITICAL(&this->impl_->artwork_mux);
  }

  artwork::DecodeStatus decode_status = artwork::DecodeStatus::kDecodeFailed;
  if (target != nullptr && current()) {
    decode_status = artwork::DecodeArtwork(
        encoded, encoded_size, target, artwork::kArtworkPixelCount, nullptr,
        ArtworkCancelled, &cancellation);
  }

  uint8_t *previous_encoded = nullptr;
  if (!reused) {
    taskENTER_CRITICAL(&this->impl_->artwork_mux);
    const bool owns_target =
        target_slot >= 0 &&
        target_slot < static_cast<int8_t>(artwork::kArtworkSlotCount) &&
        this->impl_->writing_slot == target_slot &&
        this->impl_->writing_identity == request.identity &&
        this->impl_->writing_revision == request.revision &&
        this->impl_->writing_generation == request.generation &&
        target == this->impl_->slots[target_slot];
    if (decode_status == artwork::DecodeStatus::kSuccess && owns_target &&
        current() && target_slot != this->impl_->published_slot &&
        this->impl_->reader_pins[target_slot] == 0) {
      previous_encoded = this->impl_->published_encoded;
      this->impl_->published_slot = target_slot;
      this->impl_->published_identity = request.identity;
      this->impl_->published_revision = request.revision;
      this->impl_->published_encoded = encoded;
      this->impl_->published_encoded_size = encoded_size;
      this->impl_->published_encoded_slot = target_slot;
      encoded = nullptr;
      completion.succeeded = true;
    }
    if (owns_target) {
      this->impl_->writing_slot = -1;
      this->impl_->writing_identity = 0;
      this->impl_->writing_revision = 0;
      this->impl_->writing_generation = 0;
    }
    taskEXIT_CRITICAL(&this->impl_->artwork_mux);
  }

  if (encoded != nullptr)
    heap_caps_free(encoded);
  if (previous_encoded != nullptr)
    heap_caps_free(previous_encoded);

  if (current() && target != nullptr &&
      decode_status != artwork::DecodeStatus::kSuccess &&
      decode_status != artwork::DecodeStatus::kCancelled) {
    ESP_LOGW(TAG, "artwork decode failed (status %u)",
             static_cast<unsigned>(decode_status));
  }
  if (reused)
    ESP_LOGD(TAG, "artwork reused");
  else if (completion.succeeded)
    ESP_LOGD(TAG, "artwork updated");
  if (!this->impl_->decode_worker.StopRequested() &&
      !this->impl_->fetch_worker.StopRequested()) {
    this->impl_->completion.Publish(completion);
    this->impl_->completion_ready.store(true, std::memory_order_release);
  }
  this->impl_->in_flight.store(false, std::memory_order_release);
}

bool HomeAssistantMediaSource::FetchArtwork_(const char *url, size_t url_size,
                                             uint32_t generation,
                                             uint8_t **encoded,
                                             size_t *encoded_size) {
  if (this->impl_ == nullptr || encoded == nullptr || encoded_size == nullptr)
    return false;
  *encoded = nullptr;
  *encoded_size = 0;
  if (url == nullptr || url_size == 0 || this->http_ == nullptr ||
      this->http_gate_ == nullptr)
    return false;
  const auto cancelled = [this, generation]() {
    return this->impl_->fetch_worker.StopRequested() ||
           !this->impl_->eligible.load(std::memory_order_acquire) ||
           this->impl_->generation.load(std::memory_order_acquire) !=
               generation;
  };

  const http_body::ReadOptions options{artwork::kMaxEncodedBytes,
                                       kArtworkReadChunkBytes,
                                       MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT};
  auto body =
      http_body::GetBounded(this->http_, this->http_gate_,
                            std::string(url, url_size), options, cancelled);
  if (!body.succeeded()) {
    if (body.status != http_body::ReadStatus::kCancelled) {
      ESP_LOGW(TAG, "artwork body request failed (%s, status %d, %zu bytes)",
               http_body::ReadStatusName(body.status), body.http_status,
               body.received_bytes);
    }
    return false;
  }

  if (cancelled())
    return false;
  const artwork::ImageMagic candidate_magic =
      artwork::ClassifyMagic(body.body.data(), body.received_bytes);
  if (candidate_magic != artwork::ImageMagic::kPng &&
      candidate_magic != artwork::ImageMagic::kJpeg) {
    ESP_LOGW(TAG, "artwork body has unsupported magic (%zu bytes)",
             body.received_bytes);
    return false;
  }
  *encoded = body.body.Release();
  *encoded_size = body.received_bytes;
  ESP_LOGD(TAG, "artwork fetched: %zu bytes", *encoded_size);
  return true;
}

void HomeAssistantMediaSource::OnSubscriptionRoot_(StringRef value) {
  if (this->initial_snapshot_started_) {
    this->initial_root_state_ =
        pixoo::now_playing::ParsePlaybackState(value.c_str(), value.size());
    this->initial_root_seen_ = true;
    return;
  }
  this->OnRoot_(value);
}

void HomeAssistantMediaSource::OnRoot_(StringRef value) {
  this->OnRootState_(
      pixoo::now_playing::ParsePlaybackState(value.c_str(), value.size()),
      millis());
}

void HomeAssistantMediaSource::OnRootState_(
    pixoo::now_playing::PlaybackState state, uint32_t now_ms) {
  pixoo::now_playing::NowPlayingData data;
  if (this->policy_.OnPlaybackState(state, now_ms, &data))
    this->Publish_(data);
  if (pixoo::now_playing::IsActivePlaybackState(state)) {
    this->position_context_allowed_ =
        this->configured_ && this->transport_connected_;
  } else {
    this->position_context_allowed_ = false;
    this->ClearPositionTracking_();
  }
  if (pixoo::now_playing::IsInactivePlaybackState(state))
    this->ClearDesiredArtwork_();
  this->connected_grace_active_ = false;
}

void HomeAssistantMediaSource::OnContentId_(StringRef value) {
  if (value.size() >
      pixoo::now_playing::NowPlayingMetadataPolicy::kMaxContentIdBytes)
    return;
  const uint32_t now_ms = millis();
  if (this->position_context_allowed_ &&
      this->ObserveContentId_(value.c_str(), value.size())) {
    const bool restage_raw =
        this->raw_position_pending_content_id_ && this->has_last_position_;
    const double raw_seconds = this->last_position_seconds_;
    const uint32_t raw_receipt_ms = this->last_position_received_ms_;
    this->ClearPositionTracking_();
    if (restage_raw) {
      this->last_position_seconds_ = raw_seconds;
      this->last_position_received_ms_ = raw_receipt_ms;
      this->has_last_position_ = true;
      this->StagePosition_(raw_seconds, raw_receipt_ms);
    }
  }
  this->raw_position_pending_content_id_ = false;
  this->policy_.OnContentId(value.c_str(), value.size(), now_ms);
}

void HomeAssistantMediaSource::OnTitle_(StringRef value) {
  this->policy_.OnTitle(value.c_str(), value.size(), millis());
}

void HomeAssistantMediaSource::OnArtist_(StringRef value) {
  this->policy_.OnArtist(value.c_str(), value.size(), millis());
}

void HomeAssistantMediaSource::OnDuration_(StringRef value) {
  if (value.empty()) {
    this->policy_.OnDuration(false, 0.0, millis());
    return;
  }
  double seconds = 0.0;
  if (ParseFiniteDecimal(value, &seconds))
    this->policy_.OnDuration(true, seconds, millis());
}

void HomeAssistantMediaSource::ClearPositionTracking_() {
  this->last_position_seconds_ = 0.0;
  this->last_position_received_ms_ = 0;
  this->position_timestamp_ms_utc_ = 0;
  this->has_last_position_ = false;
  this->has_position_timestamp_ = false;
  this->raw_position_pending_content_id_ = false;
}

bool HomeAssistantMediaSource::ObserveContentId_(const char *value,
                                                 size_t size) {
  const bool has_explicit = size != 0;
  const bool changed =
      this->content_id_observed_ &&
      (has_explicit != this->has_explicit_content_id_ ||
       (has_explicit &&
        (size != this->last_content_id_size_ ||
         std::memcmp(value, this->last_content_id_, size) != 0)));
  this->content_id_observed_ = true;
  this->has_explicit_content_id_ = has_explicit;
  this->last_content_id_size_ = static_cast<uint16_t>(size);
  if (has_explicit)
    std::memcpy(this->last_content_id_, value, size);
  this->last_content_id_[size] = '\0';
  return changed;
}

void HomeAssistantMediaSource::StagePosition_(double seconds,
                                              uint32_t receipt_ms) {
  double adjusted = seconds;
  if (this->has_position_timestamp_ && this->clock_ != nullptr) {
    const ESPTime now = this->clock_->utcnow();
    if (now.is_valid()) {
      const int64_t elapsed_ms = static_cast<int64_t>(now.timestamp) * 1000 -
                                 this->position_timestamp_ms_utc_;
      if (elapsed_ms >= -kTimestampFutureToleranceSeconds * 1000 &&
          elapsed_ms <= kTimestampOldLimitSeconds * 1000)
        adjusted +=
            elapsed_ms > 0 ? static_cast<double>(elapsed_ms) / 1000.0 : 0.0;
    }
  }
  this->policy_.OnPosition(true, adjusted, receipt_ms);
}

void HomeAssistantMediaSource::OnPosition_(StringRef value) {
  const uint32_t receipt_ms = millis();
  if (!this->position_context_allowed_)
    return;
  if (value.empty()) {
    this->ClearPositionTracking_();
    this->policy_.OnPosition(false, 0.0, receipt_ms);
    return;
  }
  double seconds = 0.0;
  if (!ParseFiniteDecimal(value, &seconds))
    return;
  this->last_position_seconds_ = seconds;
  this->last_position_received_ms_ = receipt_ms;
  this->has_last_position_ = true;
  this->raw_position_pending_content_id_ = true;
  this->StagePosition_(seconds, receipt_ms);
}

void HomeAssistantMediaSource::OnPositionUpdatedAt_(StringRef value) {
  if (!this->position_context_allowed_) {
    this->ClearPositionTracking_();
    return;
  }
  int64_t timestamp_ms = 0;
  this->has_position_timestamp_ = ParseHaUtcTimestampMs(value, &timestamp_ms);
  if (this->has_position_timestamp_)
    this->position_timestamp_ms_utc_ = timestamp_ms;
  else
    this->position_timestamp_ms_utc_ = 0;
  if (this->has_last_position_)
    this->StagePosition_(this->last_position_seconds_, millis());
}

void HomeAssistantMediaSource::OnEntityPicture_(StringRef value) {
  const uint32_t now_ms = millis();
  std::string resolved;
  if (value.empty() || !now_playing_config::ResolveArtworkUrl(
                           this->record_.home_assistant_url,
                           this->record_.home_assistant_url_size, value.c_str(),
                           value.size(), &resolved)) {
    this->ClearDesiredArtwork_();
    this->policy_.OnArtworkIdentity(false, 0, now_ms);
    return;
  }
  // Identity follows the exact resolved request URL. Equivalent relative and
  // absolute spellings therefore reuse one decoded image, while any signed
  // path/query change still invalidates it.
  const uint64_t identity = pixoo::now_playing::HashNowPlayingBytes(
      kArtworkIdentityDomain, resolved.data(), resolved.size());
  const bool changed = this->desired_artwork_url_size_ != resolved.size() ||
                       std::memcmp(this->desired_artwork_url_, resolved.data(),
                                   resolved.size()) != 0;
  this->desired_artwork_url_size_ = static_cast<uint16_t>(resolved.size());
  std::memcpy(this->desired_artwork_url_, resolved.data(), resolved.size());
  this->desired_artwork_url_[resolved.size()] = '\0';
  this->desired_artwork_identity_ = identity;
  if (changed) {
    this->artwork_fetch_policy_.SetDesired(identity, true);
    if (this->impl_ != nullptr) {
      taskENTER_CRITICAL(&this->impl_->artwork_mux);
      this->impl_->generation.store(this->artwork_fetch_policy_.generation(),
                                    std::memory_order_release);
      taskEXIT_CRITICAL(&this->impl_->artwork_mux);
    }
  }
  this->policy_.OnArtworkIdentity(true, identity, now_ms);
}

bool HomeAssistantMediaSource::SaveAndSync_(
    const now_playing_config::ConfigRecord &record) {
  return global_preferences != nullptr && this->preference_.save(&record) &&
         global_preferences->sync();
}

bool HomeAssistantMediaSource::PersistAtomically_(
    const now_playing_config::ConfigRecord &next) {
  const now_playing_config::ConfigRecord previous = this->record_;
  if (this->SaveAndSync_(next))
    return true;
  // ESPHome preferences stage writes before sync(). Restore the last committed
  // logical value so a later successful global sync cannot commit a rejected
  // configuration. If rollback itself fails, rebooting remains unsafe and the
  // current process keeps its prior configuration.
  if (!this->SaveAndSync_(previous))
    ESP_LOGE(TAG, "configuration rollback failed");
  return false;
}

bool HomeAssistantMediaSource::Configure(
    const std::string &entity_id, const std::string &home_assistant_url) {
  const uint32_t next_revision = this->config_revision_ + 1;
  if (next_revision == 0) {
    ESP_LOGW(TAG, "configuration revision exhausted");
    return false;
  }
  now_playing_config::ConfigRecord next{};
  if (!now_playing_config::MakeConfigRecord(
          entity_id.data(), entity_id.size(), home_assistant_url.data(),
          home_assistant_url.size(), next_revision, &next)) {
    ESP_LOGW(TAG, "configuration validation failed");
    return false;
  }
  if (this->configured_ &&
      next.entity_id_size == this->record_.entity_id_size &&
      next.home_assistant_url_size == this->record_.home_assistant_url_size &&
      std::memcmp(next.entity_id, this->record_.entity_id,
                  next.entity_id_size) == 0 &&
      std::memcmp(next.home_assistant_url, this->record_.home_assistant_url,
                  next.home_assistant_url_size) == 0)
    return false;
  if (!this->PersistAtomically_(next)) {
    ESP_LOGW(TAG, "configuration persistence failed");
    return false;
  }
  this->record_ = next;
  this->configured_ = true;
  this->config_revision_ = next.revision;
  this->ResetState_(millis());
  this->defer("now_playing_reboot", []() { App.safe_reboot(); });
  return true;
}

bool HomeAssistantMediaSource::Clear() {
  if (!this->configured_)
    return false;
  const now_playing_config::ConfigRecord empty{};
  if (!this->PersistAtomically_(empty)) {
    ESP_LOGW(TAG, "configuration persistence failed");
    return false;
  }
  this->record_ = {};
  this->configured_ = false;
  ++this->config_revision_;
  if (this->config_revision_ == 0)
    this->config_revision_ = 1;
  this->ResetState_(millis());
  this->defer("now_playing_reboot", []() { App.safe_reboot(); });
  return true;
}

} // namespace esphome::pixoo64::adapters

#endif  // USE_PIXOO64_NOW_PLAYING
