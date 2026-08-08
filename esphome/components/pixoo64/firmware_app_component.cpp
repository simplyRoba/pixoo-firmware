#include "firmware_app_component.h"

#include "esphome/core/hal.h"
#include "esphome/core/log.h"

namespace esphome::pixoo64 {
namespace {

static const char *const TAG = "pixoo64.app";
}  // namespace

void FirmwareAppComponent::setup() {
  if (this->panel_ == nullptr || this->panel_component_ == nullptr ||
      this->renderer_ == nullptr || this->light_ == nullptr ||
      this->dashboard_select_ == nullptr) {
    ESP_LOGE(TAG, "panel, renderer, light, and dashboard select are required");
    this->mark_failed();
    return;
  }
  if (this->panel_component_->is_failed()) {
    ESP_LOGE(TAG, "panel adapter setup failed");
    this->mark_failed();
    return;
  }

  const std::string dashboard_id = this->dashboard_select_->current_option().c_str();

  this->app_.emplace(*this->panel_, *this->renderer_, this->sound_player_,
                     this->microphone_, this, pixoo::FirmwareAppConfig{}, this,
                     this);
  if (this->frame_metrics_window_ms_ != 0)
    this->frame_metrics_window_.Reset(millis());
  this->started_ = this->app_->Start(millis(), this->ReadLight_(), dashboard_id);
  if (!this->started_)
    ESP_LOGE(TAG, "application could not resolve a dashboard");
}

void FirmwareAppComponent::update() {
  const uint32_t now_ms = millis();
  if (this->started_)
    this->app_->Tick(now_ms);
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

void FirmwareAppComponent::SyncLightFromEntity() {
  if (this->started_)
    this->app_->SetUserLight(this->ReadLight_(), millis());
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
                                            const std::string &severity,
                                            int32_t duration_seconds,
                                            const std::string &sound) {
  if (!this->started_)
    return;
  if (text.size() > pixoo::kMaximumNotificationTextBytes) {
    ESP_LOGW(TAG, "notification rejected: text exceeds %u bytes",
             static_cast<unsigned>(pixoo::kMaximumNotificationTextBytes));
    return;
  }
  pixoo::NotificationRequest request;
  request.notification = pixoo::Notification{text, pixoo::ParseSeverity(severity)};
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
    this->app_->ClearOverlayQueue();
}

void FirmwareAppComponent::BeginFirmwareUpdate() {
  if (this->started_ && !this->app_->BeginFirmwareUpdate())
    ESP_LOGW(TAG, "firmware update screen could not be presented");
}

void FirmwareAppComponent::Publish(pixoo::LightState state) {
  if (this->light_ == nullptr)
    return;
  this->light_->make_call()
      .set_state(state.on)
      .set_brightness(state.brightness)
      .set_transition_length(0)
      .perform();
}

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

}  // namespace esphome::pixoo64
