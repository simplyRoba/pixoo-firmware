#include "firmware_app_component.h"
#include "pixoo64.h"

#include <cmath>
#include <cstdlib>
#include <esp_system.h>
#include <esp_heap_caps.h>
#include <limits>
#include <new>

#include <utility>
#include "esphome/core/hal.h"
#include "esphome/core/log.h"

namespace esphome::pixoo64 {
namespace {

static const char *const TAG = "pixoo64.app";
}  // namespace

FirmwareAppComponent::~FirmwareAppComponent() {
  StopWorker_();
  worker_.reset();
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

  this->worker_.reset(new (std::nothrow) RenderWorker(
      *static_cast<Pixoo64Panel *>(this->panel_component_),
      *this->renderer_, this->frame_metrics_window_ms_));
  if (!this->worker_) {
    ESP_LOGE(TAG, "render worker allocation failed");
    this->mark_failed();
    return;
  }
  ESP_LOGI(TAG, "ESPHome/API task on core %d", xPortGetCoreID());
  this->renderer_->CaptureInputs(dashboard_id);
  this->started_ = this->worker_->Start(this->ReadLight_(), dashboard_id,
                                        this->ReadSolarBrightness_());
  if (!this->started_) {
    ESP_LOGE(TAG, "render worker startup failed");
    this->mark_failed();
    return;
  }

  this->dashboard_select_->add_on_state_callback(
      [this](size_t index) { this->SelectDashboard(this->dashboard_select_->option_at(index)); });
  for (size_t i = 0; i < this->timing_buttons_.size(); ++i) {
    if (this->timing_buttons_[i] == nullptr) continue;
    this->timing_buttons_[i]->add_on_press_callback([this, i]() {
      switch (i) {
        case 0: StopwatchStart(); break;
        case 1: {
          const float seconds = timer_duration_ != nullptr ? timer_duration_->state : 300.0f;
          if (std::isfinite(seconds) && seconds >= 1.0f && seconds <= 5999.0f &&
              std::floor(seconds) == seconds)
            this->TimerStart(static_cast<int32_t>(seconds));
          else
            ESP_LOGW(TAG, "Timer duration must be a whole number between 1 and 5999 seconds");
          break;
        }
        case 2: TimingPause(); break;
        case 3: TimingResume(); break;
        case 4: TimingStop(); break;
        case 5: TimingMinimize(); break;
        case 6: TimingShow(); break;
      }
    });
  }
  this->worker_state_ = this->worker_->State();
  this->PublishTiming_(millis(), true);
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
    this->solar_refresh_requested_ = this->worker_state_.solar_enabled;
    this->RefreshSolarElevation_(millis(), true);
  }
}

void FirmwareAppComponent::update() {
  if (!this->started_ || this->stopping_) return;
  this->DrainOutputs_();
  if (this->stopping_) return;
  this->worker_state_ = this->worker_->State();
  const uint32_t now_ms = millis();
  this->RefreshSolarElevation_(now_ms, false);
  this->PublishTiming_(now_ms);
  this->PublishFrameMetrics_();
}

void FirmwareAppComponent::dump_config() {
  ESP_LOGCONFIG(TAG, "Pixoo64 firmware application:");
  if (this->microphone_ != nullptr)
    ESP_LOGCONFIG(TAG, "  Microphone mechanism configured");
  if (this->sound_player_ != nullptr)
    ESP_LOGCONFIG(TAG, "  Sound player configured");
}

void FirmwareAppComponent::SelectDashboard(const std::string &dashboard_id) {
  if (this->renderer_ != nullptr)
    this->renderer_->CaptureInputs(dashboard_id);
  WorkerCommand command; command.kind = WorkerCommandKind::kDashboard;
  command.dashboard = dashboard_id; Submit_(std::move(command));
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
  WorkerCommand command; command.kind = WorkerCommandKind::kLight; command.light = state;
  if (Submit_(std::move(command), &this->latest_light_sequence_)) pending_light_ = true;
}

void FirmwareAppComponent::SetSolarBrightnessEnabled(bool enabled) {
  if (this->publishing_solar_ || !this->started_)
    return;
  WorkerCommand command; command.kind = WorkerCommandKind::kSolarEnabled; command.enabled = enabled;
  if (Submit_(std::move(command), &this->latest_solar_sequence_)) {
    pending_solar_ = true;
    this->solar_refresh_requested_ = enabled;
  }
}

void FirmwareAppComponent::SolarBrightnessLevelsChanged() {
  if (!this->started_)
    return;
  const float day = this->solar_day_brightness_->state / 100.0f;
  const float night = this->solar_night_brightness_->state / 100.0f;
  WorkerCommand command; command.kind = WorkerCommandKind::kSolarLevels;
  command.value = day; command.second_value = night; Submit_(std::move(command));
}

void FirmwareAppComponent::SolarLocationChanged() {
  if (this->started_)
    this->solar_refresh_requested_ = true;
}

#define PIXOO_SIMPLE_COMMAND(Name, Kind) \
  void FirmwareAppComponent::Name() { \
    WorkerCommand command; command.kind = WorkerCommandKind::Kind; Submit_(std::move(command)); \
  }
PIXOO_SIMPLE_COMMAND(PowerButtonPressed, kPowerPressed)
PIXOO_SIMPLE_COMMAND(PowerButtonReleased, kPowerReleased)
PIXOO_SIMPLE_COMMAND(BrightnessButtonPressed, kBrightnessPressed)
PIXOO_SIMPLE_COMMAND(BrightnessButtonReleased, kBrightnessReleased)
PIXOO_SIMPLE_COMMAND(ClearOverlayQueue, kClearOverlays)
PIXOO_SIMPLE_COMMAND(StopwatchStart, kStopwatchStart)
PIXOO_SIMPLE_COMMAND(TimingPause, kTimingPause)
PIXOO_SIMPLE_COMMAND(TimingResume, kTimingResume)
PIXOO_SIMPLE_COMMAND(TimingStop, kTimingStop)
PIXOO_SIMPLE_COMMAND(TimingMinimize, kTimingMinimize)
PIXOO_SIMPLE_COMMAND(TimingShow, kTimingShow)
#undef PIXOO_SIMPLE_COMMAND

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
  void *memory = heap_caps_malloc(sizeof(pixoo::NotificationRequest),
                                  MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (memory == nullptr) {
    ESP_LOGW(TAG, "notification rejected: PSRAM allocation failed");
    return;
  }
  WorkerCommand command; command.kind = WorkerCommandKind::kNotification;
  command.notification.reset(new (memory) pixoo::NotificationRequest(std::move(request)));
  Submit_(std::move(command));
}

void FirmwareAppComponent::ShowReaction(const std::string &reaction_name) {
  if (!this->started_)
    return;
  const pixoo::Reaction reaction = pixoo::ParseReaction(reaction_name);
  if (reaction_name != pixoo::ReactionName(reaction)) {
    ESP_LOGW(TAG, "unknown reaction: %s", reaction_name.c_str());
    return;
  }
  WorkerCommand command; command.kind = WorkerCommandKind::kReaction;
  command.reaction = reaction; Submit_(std::move(command));
}

void FirmwareAppComponent::BeginFirmwareUpdate() {
  if (!worker_ || worker_->IsStopped()) return;
  uint32_t sequence = ++sequence_;
  if (sequence == 0) sequence = ++sequence_;
  const bool requested = worker_->RequestFirmwareUpdate(sequence);
  if (!requested) worker_->RequestStop();
  const uint32_t start = millis();
  while (millis() - start < 3000) {
    DrainOutputs_(false);
    if (worker_->IsStopped()) {
      DrainOutputs_();
      return;
    }
    const auto state = worker_->State();
    if (requested && state.update_completed_sequence == sequence) {
      if (!state.update_succeeded)
        ESP_LOGW(TAG, "firmware update screen could not be presented");
      DrainOutputs_();
      return;
    }
    delay(1);
  }
  ESP_LOGE(TAG, "worker did not acknowledge OTA pause; restarting before flash write");
  DrainOutputs_();
  esp_restart();
  std::abort();
}

void FirmwareAppComponent::EndFirmwareUpdate() {
  if (worker_ && !stopping_) worker_->ResumeFirmwareUpdate();
}

bool FirmwareAppComponent::TimerStart(int32_t duration_seconds) {
  if (duration_seconds < 1 || duration_seconds > 5999) return false;
  WorkerCommand command; command.kind = WorkerCommandKind::kTimerStart;
  command.integer = duration_seconds; return Submit_(std::move(command));
}

void FirmwareAppComponent::PublishTiming_(uint32_t now_ms, bool force) {
  const auto &snapshot = this->worker_state_.timing;
  const bool transition = !timing_published_ || snapshot.kind != published_timing_.kind ||
                          snapshot.phase != published_timing_.phase;
  if (transition) {
    if (timing_mode_ != nullptr) timing_mode_->publish_state(pixoo::TimingKindName(snapshot.kind));
    if (timing_state_ != nullptr) timing_state_->publish_state(pixoo::TimingPhaseName(snapshot.phase));
  }
  const bool visibility_changed = !timing_published_ ||
                                  snapshot.visible() != published_timing_.visible();
  if (visibility_changed && timing_visible_ != nullptr)
    timing_visible_->publish_state(snapshot.visible());
  if (force || transition || visibility_changed ||
      (snapshot.running() && now_ms - timing_published_ms_ >= 1000)) {
    if (stopwatch_elapsed_ != nullptr)
      stopwatch_elapsed_->publish_state(snapshot.kind == pixoo::TimingKind::kStopwatch ? snapshot.value_ms / 1000.0f : 0.0f);
    if (timer_remaining_ != nullptr)
      timer_remaining_->publish_state(snapshot.kind == pixoo::TimingKind::kTimer ? snapshot.value_ms / 1000.0f : 0.0f);
    timing_published_ms_ = now_ms;
    published_timing_ = snapshot;
    timing_published_ = true;
  }
}

void FirmwareAppComponent::RequestReboot() {
  if (!worker_ || !worker_->RequestReboot()) Reboot();
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
  this->publishing_solar_ = true;
  this->solar_brightness_switch_->publish_state(enabled);
  this->publishing_solar_ = false;
}

void FirmwareAppComponent::Reboot() {
  if (sound_player_ != nullptr) sound_player_->Stop();
  App.safe_reboot();
}

void FirmwareAppComponent::FactoryReset() {
  if (sound_player_ != nullptr) sound_player_->Stop();
  if (global_preferences != nullptr)
    global_preferences->reset();
  App.safe_reboot();
}

void FirmwareAppComponent::PublishFrameMetrics_() {
  if (worker_state_.metrics_generation == metrics_generation_) return;
  metrics_generation_ = worker_state_.metrics_generation;
  const auto &snapshot = worker_state_.metrics;
  if (this->frame_average_sensor_ != nullptr)
    this->frame_average_sensor_->publish_state(snapshot.average_ms);
  if (this->frame_max_sensor_ != nullptr)
    this->frame_max_sensor_->publish_state(snapshot.maximum_ms);
  if (this->rendered_fps_sensor_ != nullptr)
    this->rendered_fps_sensor_->publish_state(snapshot.frames_per_second);
  if (this->sent_fps_sensor_ != nullptr)
    this->sent_fps_sensor_->publish_state(snapshot.sent_frames_per_second);
  if (this->late_frames_sensor_ != nullptr)
    this->late_frames_sensor_->publish_state(snapshot.late_frames);
  if (this->busy_drops_sensor_ != nullptr)
    this->busy_drops_sensor_->publish_state(snapshot.busy_drops);
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
  (void) now_ms;
  if (!this->started_ || this->stopping_ || this->solar_brightness_switch_ == nullptr) return;
  if (pending_elevation_) {
    if (WorkerSequenceBefore(worker_state_.processed_sequence, pending_elevation_sequence_)) return;
    pending_elevation_ = false;
  }
  if (!worker_state_.solar_enabled) return;
  if (!force && !solar_refresh_requested_ && !worker_state_.solar_due) return;
  const float latitude = solar_latitude_->state;
  const float longitude = solar_longitude_->state;
  float elevation = std::numeric_limits<float>::quiet_NaN();
  if (std::isfinite(latitude) && std::isfinite(longitude) &&
      latitude >= -90.0f && latitude <= 90.0f && longitude >= -180.0f && longitude <= 180.0f) {
    solar_sun_->set_latitude(latitude);
    solar_sun_->set_longitude(longitude);
    elevation = static_cast<float>(solar_sun_->elevation());
  }
  WorkerCommand command; command.kind = WorkerCommandKind::kSolarElevation; command.value = elevation;
  if (Submit_(std::move(command), &pending_elevation_sequence_)) {
    pending_elevation_ = true;
    solar_refresh_requested_ = false;
  } else {
    solar_refresh_requested_ = true;
  }
}

bool FirmwareAppComponent::Submit_(WorkerCommand command, uint32_t *sequence) {
  if (!started_ || stopping_ || !worker_) return false;
  command.timestamp_ms = millis();
  command.sequence = ++sequence_;
  if (command.sequence == 0) command.sequence = ++sequence_;
  const uint32_t admitted = command.sequence;
  if (!worker_->Submit(std::move(command))) {
    ESP_LOGW(TAG, "render worker command queue full or unavailable");
    return false;
  }
  if (sequence != nullptr) *sequence = admitted;
  return true;
}

void FirmwareAppComponent::DrainOutputs_(bool allow_reboot) {
  if (!worker_) return;
  auto run_pending_control = [this, allow_reboot]() {
    if (!allow_reboot || stopping_) return false;
    if (pending_factory_reset_) {
      pending_factory_reset_ = false;
      pending_reboot_ = false;
      FactoryReset();
      return true;
    }
    if (pending_reboot_) {
      pending_reboot_ = false;
      Reboot();
      return true;
    }
    return false;
  };
  WorkerOutput output;
  while (worker_->ReadOutput(&output)) {
    if (output.kind == WorkerOutputKind::kReboot) {
      pending_reboot_ = true;
      if (run_pending_control()) return;
      continue;
    }
    if (output.kind == WorkerOutputKind::kFactoryReset) {
      pending_factory_reset_ = true;
      if (run_pending_control()) return;
      continue;
    }
    if (run_pending_control()) return;
    switch (output.kind) {
      case WorkerOutputKind::kLight:
        if (!pending_light_ || !WorkerSequenceBefore(output.sequence, latest_light_sequence_)) {
          pending_light_ = false;
          Publish(output.light, output.flag);
        }
        break;
      case WorkerOutputKind::kSolarEnabled:
        if (!pending_solar_ || !WorkerSequenceBefore(output.sequence, latest_solar_sequence_)) {
          pending_solar_ = false;
          PublishSolarBrightnessEnabled(output.flag);
        }
        break;
      case WorkerOutputKind::kSoundPlay: if (sound_player_) sound_player_->Play(output.sound); break;
      case WorkerOutputKind::kSoundStop: if (sound_player_) sound_player_->Stop(); break;
      case WorkerOutputKind::kMicrophoneEnabled:
        if (microphone_ && !stopping_) microphone_->SetEnabled(output.flag);
        break;
      case WorkerOutputKind::kReboot:
      case WorkerOutputKind::kFactoryReset:
        break;
    }
    if (stopping_ && allow_reboot) return;
  }
  run_pending_control();
}

void FirmwareAppComponent::StopWorker_() {
  if (stopping_) return;
  stopping_ = true;
  started_ = false;
  if (microphone_) microphone_->SetEnabled(false);
  if (worker_) worker_->RequestStop();
}

void FirmwareAppComponent::on_shutdown() { StopWorker_(); }
void FirmwareAppComponent::on_powerdown() { StopWorker_(); }
bool FirmwareAppComponent::teardown() {
  StopWorker_();
  DrainOutputs_(false);
  return !worker_ || worker_->IsStopped();
}

}  // namespace esphome::pixoo64
