#include "firmware_app_component.h"

#include <cmath>
#include <limits>
#include <new>

#include "esp_heap_caps.h"
#include "esphome/core/hal.h"
#include "esphome/core/log.h"

namespace esphome::pixoo64 {
namespace {

static const char *const TAG = "pixoo64.app";
}  // namespace

FirmwareAppComponent::~FirmwareAppComponent() {
  this->app_.reset();
  if (this->overlay_queue_storage_ != nullptr) {
    this->overlay_queue_storage_->~OverlayQueueStorage();
    heap_caps_free(this->overlay_queue_storage_);
  }
}

void FirmwareAppComponent::setup() {
  if (this->panel_ == nullptr || this->panel_component_ == nullptr ||
      this->renderer_ == nullptr || this->light_ == nullptr ||
      this->dashboard_select_ == nullptr) {
    ESP_LOGE(TAG, "panel, renderer, light, and dashboard select are required");
    this->mark_failed();
    return;
  }
  const bool has_solar = this->solar_brightness_switch_ != nullptr ||
                         this->solar_day_brightness_ != nullptr ||
                         this->solar_night_brightness_ != nullptr ||
                         this->solar_latitude_ != nullptr ||
                         this->solar_longitude_ != nullptr ||
                         this->solar_sun_ != nullptr;
  if (has_solar && (this->solar_brightness_switch_ == nullptr ||
                    this->solar_day_brightness_ == nullptr ||
                    this->solar_night_brightness_ == nullptr ||
                    this->solar_latitude_ == nullptr ||
                    this->solar_longitude_ == nullptr ||
                    this->solar_sun_ == nullptr)) {
    ESP_LOGE(TAG, "solar brightness wiring is incomplete");
    this->mark_failed();
    return;
  }
  if (this->panel_component_->is_failed()) {
    ESP_LOGE(TAG, "panel adapter setup failed");
    this->mark_failed();
    return;
  }

  const std::string dashboard_id = this->dashboard_select_->current_option().c_str();

  void *queue_memory = heap_caps_malloc(
      sizeof(pixoo::OverlayQueueStorage), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (queue_memory == nullptr) {
    ESP_LOGW(TAG, "overlay queue storage using internal RAM fallback");
    queue_memory = heap_caps_malloc(sizeof(pixoo::OverlayQueueStorage),
                                   MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  }
  if (queue_memory == nullptr) {
    ESP_LOGE(TAG, "overlay queue storage allocation failed");
    this->mark_failed();
    return;
  }
  this->overlay_queue_storage_ =
      new (queue_memory) pixoo::OverlayQueueStorage{};

  this->app_.emplace(
      *this->panel_, *this->renderer_, this->sound_player_, this->microphone_,
      this, pixoo::FirmwareAppConfig{}, this, this,
      this->overlay_queue_storage_, has_solar ? this : nullptr);
  if (this->frame_metrics_window_ms_ != 0)
    this->frame_metrics_window_.Reset(millis());
  this->started_ = this->app_->Start(millis(), this->ReadLight_(), dashboard_id,
                                     this->ReadSolarBrightness_());
  if (!this->started_) {
    ESP_LOGE(TAG, "application could not resolve a dashboard");
    return;
  }

  this->light_->add_remote_values_listener(this);
  if (has_solar) {
    this->solar_brightness_switch_->add_on_state_callback(
        [this](bool enabled) { this->SetSolarBrightnessEnabled(enabled); });
    this->solar_day_brightness_->add_on_state_callback(
        [this](float) { this->SolarBrightnessLevelsChanged(); });
    this->solar_night_brightness_->add_on_state_callback(
        [this](float) { this->SolarBrightnessLevelsChanged(); });
    this->solar_latitude_->add_on_state_callback(
        [this](float) { this->SolarLocationChanged(); });
    this->solar_longitude_->add_on_state_callback(
        [this](float) { this->SolarLocationChanged(); });
    this->solar_refresh_requested_ = this->app_->solar_brightness_enabled();
    this->RefreshSolarElevation_(millis(), true);
  }
}

void FirmwareAppComponent::update() {
  const uint32_t now_ms = millis();
  if (this->started_) {
    this->RefreshSolarElevation_(now_ms, false);
    this->app_->Tick(now_ms);
  }
  this->PublishFrameMetrics_(millis());
}

void FirmwareAppComponent::dump_config() {
  ESP_LOGCONFIG(TAG, "Pixoo64 firmware application:");
  if (this->microphone_ != nullptr)
    ESP_LOGCONFIG(TAG, "  Microphone mechanism configured");
  if (this->sound_player_ != nullptr)
    ESP_LOGCONFIG(TAG, "  Sound player configured");
}

void FirmwareAppComponent::SelectDashboard(const std::string &dashboard_id) {
  if (this->started_)
    this->app_->SelectDashboard(dashboard_id);
}

void FirmwareAppComponent::on_light_remote_values_update() {
  this->SyncLightFromEntity();
}

void FirmwareAppComponent::SyncLightFromEntity() {
  if (!this->started_)
    return;
  const pixoo::LightState state = this->ReadLight_();
  if (this->light_publication_guard_.ConsumeIfExpected(state))
    return;
  this->app_->SetUserLight(state, millis());
}

void FirmwareAppComponent::SetSolarBrightnessEnabled(bool enabled) {
  if (!this->started_)
    return;
  this->app_->SetSolarBrightnessEnabled(enabled, millis());
  this->solar_refresh_requested_ = enabled;
}

void FirmwareAppComponent::SolarBrightnessLevelsChanged() {
  if (!this->started_)
    return;
  const float day = this->solar_day_brightness_->state / 100.0f;
  const float night = this->solar_night_brightness_->state / 100.0f;
  this->app_->SetSolarBrightnessLevels(day, night, millis());
}

void FirmwareAppComponent::SolarLocationChanged() {
  if (this->started_)
    this->solar_refresh_requested_ = true;
}

void FirmwareAppComponent::PowerButtonPressed() {
  if (this->started_)
    this->app_->PowerButtonPressed(millis());
}

void FirmwareAppComponent::PowerButtonReleased() {
  if (this->started_)
    this->app_->PowerButtonReleased(millis());
}

void FirmwareAppComponent::BrightnessButtonPressed() {
  if (this->started_)
    this->app_->BrightnessButtonPressed(millis());
}

void FirmwareAppComponent::BrightnessButtonReleased() {
  if (this->started_)
    this->app_->BrightnessButtonReleased(millis());
}

void FirmwareAppComponent::ShowNotification(const std::string &text,
                                            const std::string &title,
                                            const std::string &severity,
                                            int32_t duration_seconds,
                                            const std::string &sound) {
  if (!this->started_)
    return;
  if (text.size() > pixoo::kMaximumNotificationTextBytes ||
      title.size() > pixoo::kMaximumNotificationTextBytes) {
    ESP_LOGW(TAG, "notification rejected: text or title exceeds %u bytes",
             static_cast<unsigned>(pixoo::kMaximumNotificationTextBytes));
    return;
  }
  pixoo::NotificationRequest request;
  request.notification =
      pixoo::Notification{text, pixoo::ParseSeverity(severity), title};
  request.requested_duration_ms =
      pixoo::NotificationDurationMsFromSeconds(duration_seconds);
  request.has_sound = ParseSound_(sound, &request.sound);
  if (!this->app_->Notify(std::move(request), millis()))
    ESP_LOGW(TAG, "overlay queue full; notification rejected");
}

void FirmwareAppComponent::ShowReaction(const std::string &reaction_name) {
  if (!this->started_)
    return;
  const pixoo::Reaction reaction = pixoo::ParseReaction(reaction_name);
  if (reaction_name != pixoo::ReactionName(reaction)) {
    ESP_LOGW(TAG, "unknown reaction: %s", reaction_name.c_str());
    return;
  }
  if (!this->app_->React(reaction, millis()))
    ESP_LOGW(TAG, "reaction queue full: %s", reaction_name.c_str());
}

void FirmwareAppComponent::ClearOverlayQueue() {
  if (this->started_)
    this->app_->ClearOverlayQueue(millis());
}

void FirmwareAppComponent::BeginFirmwareUpdate() {
  if (this->started_ && !this->app_->BeginFirmwareUpdate(millis()))
    ESP_LOGW(TAG, "firmware update screen could not be presented");
}

void FirmwareAppComponent::StopwatchStart() {
  if (this->started_)
    this->app_->StopwatchStart(millis());
}

void FirmwareAppComponent::StopwatchStop() {
  if (this->started_)
    this->app_->StopwatchStop(millis());
}

void FirmwareAppComponent::StopwatchReset() {
  if (this->started_)
    this->app_->StopwatchReset(millis());
}

void FirmwareAppComponent::TimerSet(int32_t duration_ms) {
  if (this->started_)
    this->app_->TimerSet(pixoo::TimerDurationMsFromApi(duration_ms), millis());
}

void FirmwareAppComponent::TimerStart() {
  if (this->started_)
    this->app_->TimerStart(millis());
}

void FirmwareAppComponent::TimerStop() {
  if (this->started_)
    this->app_->TimerStop(millis());
}

void FirmwareAppComponent::TimerReset() {
  if (this->started_)
    this->app_->TimerReset(millis());
}

void FirmwareAppComponent::RequestReboot() {
  if (this->app_.has_value())
    this->app_->Reboot();
  else
    this->Reboot();
}

void FirmwareAppComponent::Publish(pixoo::LightState state, bool persistent) {
  if (this->light_ == nullptr)
    return;
  this->light_publication_guard_.Expect(state);
  this->light_->make_call()
      .set_state(state.on)
      .set_brightness(state.brightness)
      .set_transition_length(0)
      .set_save(persistent)
      .perform();
}

void FirmwareAppComponent::PublishSolarBrightnessEnabled(bool enabled) {
  if (this->solar_brightness_switch_ == nullptr ||
      this->solar_brightness_switch_->state == enabled)
    return;
  // publish_state() persists through the switch's configured restore mode and
  // invokes the same post-publication callback as a Home Assistant change.
  this->solar_brightness_switch_->publish_state(enabled);
}

void FirmwareAppComponent::Reboot() { App.safe_reboot(); }

void FirmwareAppComponent::FactoryReset() {
  if (global_preferences != nullptr)
    global_preferences->reset();
  App.safe_reboot();
}

void FirmwareAppComponent::BeginRegularFrame() {
  if (this->frame_metrics_window_ms_ == 0)
    return;
  this->frame_started_us_ = micros();
  this->frame_timing_active_ = true;
}

void FirmwareAppComponent::EndRegularFrame() {
  if (!this->frame_timing_active_)
    return;
  this->frame_metrics_window_.Record(micros() - this->frame_started_us_);
  this->frame_timing_active_ = false;
}

void FirmwareAppComponent::PublishFrameMetrics_(uint32_t now_ms) {
  if (this->frame_metrics_window_ms_ == 0 ||
      !this->frame_metrics_window_.IsDue(now_ms,
                                         this->frame_metrics_window_ms_))
    return;

  pixoo::FrameMetricsSnapshot snapshot;
  if (!this->frame_metrics_window_.Close(now_ms, &snapshot))
    return;
  if (this->frame_average_sensor_ != nullptr)
    this->frame_average_sensor_->publish_state(snapshot.average_ms);
  if (this->frame_max_sensor_ != nullptr)
    this->frame_max_sensor_->publish_state(snapshot.maximum_ms);
  if (this->rendered_fps_sensor_ != nullptr)
    this->rendered_fps_sensor_->publish_state(snapshot.frames_per_second);
}

bool FirmwareAppComponent::ParseSound_(const std::string &name,
                                       pixoo::Sound *sound) {
  if (sound == nullptr)
    return false;
  if (name == "chirp") {
    *sound = pixoo::Sound::kChirp;
  } else if (name == "success") {
    *sound = pixoo::Sound::kSuccess;
  } else if (name == "pling1") {
    *sound = pixoo::Sound::kPling1;
  } else if (name == "pling2") {
    *sound = pixoo::Sound::kPling2;
  } else if (name == "pling3") {
    *sound = pixoo::Sound::kPling3;
  } else if (name == "pling4") {
    *sound = pixoo::Sound::kPling4;
  } else if (name == "alarm1") {
    *sound = pixoo::Sound::kAlarm1;
  } else if (name == "alarm2") {
    *sound = pixoo::Sound::kAlarm2;
  } else if (name == "alarm3") {
    *sound = pixoo::Sound::kAlarm3;
  } else {
    return false;
  }
  return true;
}

pixoo::LightState FirmwareAppComponent::ReadLight_() const {
  if (this->light_ == nullptr)
    return {};
  return pixoo::LightState{this->light_->remote_values.is_on(),
                           this->light_->remote_values.get_brightness()};
}

pixoo::SolarBrightnessConfig FirmwareAppComponent::ReadSolarBrightness_() const {
  if (this->solar_brightness_switch_ == nullptr)
    return {};
  return pixoo::SolarBrightnessConfig{
      this->solar_brightness_switch_->state,
      this->solar_day_brightness_->state / 100.0f,
      this->solar_night_brightness_->state / 100.0f};
}

void FirmwareAppComponent::RefreshSolarElevation_(uint32_t now_ms, bool force) {
  if (!this->started_ || this->solar_brightness_switch_ == nullptr)
    return;
  if (!this->app_->solar_brightness_enabled()) {
    this->solar_refresh_requested_ = false;
    return;
  }
  if (!force && !this->solar_refresh_requested_ &&
      !this->app_->SolarElevationDue(now_ms))
    return;
  this->solar_refresh_requested_ = false;

  const float latitude = this->solar_latitude_->state;
  const float longitude = this->solar_longitude_->state;
  if (!std::isfinite(latitude) || !std::isfinite(longitude) ||
      latitude < -90.0f || latitude > 90.0f || longitude < -180.0f ||
      longitude > 180.0f) {
    this->app_->SetSolarElevation(
        std::numeric_limits<float>::quiet_NaN(), now_ms);
    return;
  }
  // ESPHome's schema requires initial coordinates for its internal calculator;
  // the persisted runtime location replaces both before every use.
  this->solar_sun_->set_latitude(latitude);
  this->solar_sun_->set_longitude(longitude);
  this->app_->SetSolarElevation(
      static_cast<float>(this->solar_sun_->elevation()), now_ms);
}

}  // namespace esphome::pixoo64
