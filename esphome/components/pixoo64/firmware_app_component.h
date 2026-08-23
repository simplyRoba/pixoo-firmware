#pragma once

#include <optional>
#include <string>

#include "esphome/components/light/light_state.h"
#include "esphome/components/number/number.h"
#include "esphome/components/select/select.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/sun/sun.h"
#include "esphome/components/switch/switch.h"
#include "esphome/core/application.h"
#include "esphome/core/automation.h"
#include "esphome/core/component.h"
#include "esphome/core/preferences.h"
#include "firmware_app.h"
#include "light_state_publication_guard.h"

namespace esphome::pixoo64 {

class FirmwareAppComponent final : public PollingComponent,
                                   public light::LightRemoteValuesListener,
                                   public pixoo::LightStateSink,
                                   public pixoo::SolarBrightnessStateSink,
                                   public pixoo::SystemPort,
                                   public pixoo::FrameMetricsPort {
 public:
  ~FirmwareAppComponent() override;
  void setup() override;
  void update() override;
  void dump_config() override;
  float get_setup_priority() const override {
    return setup_priority::DATA - 2.0f;
  }

  void set_panel(pixoo::PanelPort *panel) { this->panel_ = panel; }
  void set_panel_component(Component *panel_component) {
    this->panel_component_ = panel_component;
  }
  void set_renderer(pixoo::RenderPort *renderer) { this->renderer_ = renderer; }
  void set_sound_player(pixoo::SoundPlayer *sound_player) {
    this->sound_player_ = sound_player;
  }
  void set_microphone(pixoo::MicrophonePort *microphone) {
    this->microphone_ = microphone;
  }
  void set_light(light::LightState *light) { this->light_ = light; }
  void set_dashboard_select(select::Select *dashboard_select) {
    this->dashboard_select_ = dashboard_select;
  }
  void set_solar_brightness_switch(switch_::Switch *solar_brightness_switch) {
    this->solar_brightness_switch_ = solar_brightness_switch;
  }
  void set_solar_day_brightness(number::Number *solar_day_brightness) {
    this->solar_day_brightness_ = solar_day_brightness;
  }
  void set_solar_night_brightness(number::Number *solar_night_brightness) {
    this->solar_night_brightness_ = solar_night_brightness;
  }
  void set_solar_latitude(number::Number *solar_latitude) {
    this->solar_latitude_ = solar_latitude;
  }
  void set_solar_longitude(number::Number *solar_longitude) {
    this->solar_longitude_ = solar_longitude;
  }
  void set_solar_sun(sun::Sun *solar_sun) { this->solar_sun_ = solar_sun; }
  void set_frame_metrics_window_ms(uint32_t window_ms) {
    this->frame_metrics_window_ms_ = window_ms;
  }
  void set_frame_average_sensor(sensor::Sensor *sensor) {
    this->frame_average_sensor_ = sensor;
  }
  void set_frame_max_sensor(sensor::Sensor *sensor) {
    this->frame_max_sensor_ = sensor;
  }
  void set_rendered_fps_sensor(sensor::Sensor *sensor) {
    this->rendered_fps_sensor_ = sensor;
  }
  void SelectDashboard(const std::string &dashboard_id);
  void on_light_remote_values_update() override;
  void SyncLightFromEntity();
  void SetSolarBrightnessEnabled(bool enabled);
  void SolarBrightnessLevelsChanged();
  void SolarLocationChanged();
  void PowerButtonPressed();
  void PowerButtonReleased();
  void BrightnessButtonPressed();
  void BrightnessButtonReleased();
  void ShowNotification(const std::string &text, const std::string &title,
                        const std::string &severity, int32_t duration_seconds,
                        const std::string &sound);
  void ShowReaction(const std::string &reaction);
  void ClearOverlayQueue();
  void BeginFirmwareUpdate();
  void StopwatchStart();
  void StopwatchStop();
  void StopwatchReset();
  void TimerSet(int32_t duration_ms);
  void TimerStart();
  void TimerStop();
  void TimerReset();
  void RequestReboot();

  void Publish(pixoo::LightState state, bool persistent) override;
  void PublishSolarBrightnessEnabled(bool enabled) override;
  void Reboot() override;
  void FactoryReset() override;
  void BeginRegularFrame() override;
  void EndRegularFrame() override;

 protected:
  static bool ParseSound_(const std::string &name, pixoo::Sound *sound);
  pixoo::LightState ReadLight_() const;
  pixoo::SolarBrightnessConfig ReadSolarBrightness_() const;
  void RefreshSolarElevation_(uint32_t now_ms, bool force);
  void PublishFrameMetrics_(uint32_t now_ms);

  pixoo::PanelPort *panel_{nullptr};
  Component *panel_component_{nullptr};
  pixoo::RenderPort *renderer_{nullptr};
  pixoo::SoundPlayer *sound_player_{nullptr};
  pixoo::MicrophonePort *microphone_{nullptr};
  light::LightState *light_{nullptr};
  select::Select *dashboard_select_{nullptr};
  switch_::Switch *solar_brightness_switch_{nullptr};
  number::Number *solar_day_brightness_{nullptr};
  number::Number *solar_night_brightness_{nullptr};
  number::Number *solar_latitude_{nullptr};
  number::Number *solar_longitude_{nullptr};
  sun::Sun *solar_sun_{nullptr};
  pixoo::LightStatePublicationGuard light_publication_guard_;
  bool solar_refresh_requested_{false};
  sensor::Sensor *frame_average_sensor_{nullptr};
  sensor::Sensor *frame_max_sensor_{nullptr};
  sensor::Sensor *rendered_fps_sensor_{nullptr};
  uint32_t frame_metrics_window_ms_{0};
  uint32_t frame_started_us_{0};
  bool frame_timing_active_{false};
  pixoo::FrameMetricsWindow frame_metrics_window_;
  // This precedes app_ so application destruction cannot outlive its queue.
  pixoo::OverlayQueueStorage *overlay_queue_storage_{nullptr};
  std::optional<pixoo::FirmwareApp> app_;
  bool started_{false};
};

template<typename... Ts>
class ShowNotificationAction : public Action<Ts...> {
 public:
  explicit ShowNotificationAction(FirmwareAppComponent *parent) : parent_(parent) {}

  TEMPLATABLE_VALUE(std::string, text)
  TEMPLATABLE_VALUE(std::string, title)
  TEMPLATABLE_VALUE(std::string, severity)
  TEMPLATABLE_VALUE(int32_t, duration)
  TEMPLATABLE_VALUE(std::string, sound)

  void play(const Ts &...x) override {
    this->parent_->ShowNotification(this->text_.value(x...),
                                    this->title_.value(x...),
                                    this->severity_.value(x...),
                                    this->duration_.value(x...),
                                    this->sound_.value(x...));
  }

 protected:
  FirmwareAppComponent *parent_;
};

template<typename... Ts>
class ShowReactionAction : public Action<Ts...> {
 public:
  explicit ShowReactionAction(FirmwareAppComponent *parent) : parent_(parent) {}

  TEMPLATABLE_VALUE(std::string, reaction)

  void play(const Ts &...x) override {
    this->parent_->ShowReaction(this->reaction_.value(x...));
  }

 protected:
  FirmwareAppComponent *parent_;
};

template<typename... Ts>
class StopwatchStartAction : public Action<Ts...> {
 public:
  explicit StopwatchStartAction(FirmwareAppComponent *parent)
      : parent_(parent) {}

  void play(const Ts &...x) override {
    (void) sizeof...(x);
    this->parent_->StopwatchStart();
  }

 protected:
  FirmwareAppComponent *parent_;
};

template<typename... Ts>
class StopwatchStopAction : public Action<Ts...> {
 public:
  explicit StopwatchStopAction(FirmwareAppComponent *parent)
      : parent_(parent) {}

  void play(const Ts &...x) override {
    (void) sizeof...(x);
    this->parent_->StopwatchStop();
  }

 protected:
  FirmwareAppComponent *parent_;
};

template<typename... Ts>
class StopwatchResetAction : public Action<Ts...> {
 public:
  explicit StopwatchResetAction(FirmwareAppComponent *parent)
      : parent_(parent) {}

  void play(const Ts &...x) override {
    (void) sizeof...(x);
    this->parent_->StopwatchReset();
  }

 protected:
  FirmwareAppComponent *parent_;
};

template<typename... Ts>
class TimerSetAction : public Action<Ts...> {
 public:
  explicit TimerSetAction(FirmwareAppComponent *parent) : parent_(parent) {}

  TEMPLATABLE_VALUE(int32_t, duration_ms)

  void play(const Ts &...x) override {
    this->parent_->TimerSet(this->duration_ms_.value(x...));
  }

 protected:
  FirmwareAppComponent *parent_;
};

template<typename... Ts>
class TimerStartAction : public Action<Ts...> {
 public:
  explicit TimerStartAction(FirmwareAppComponent *parent) : parent_(parent) {}
  void play(const Ts &...x) override {
    (void) sizeof...(x);
    this->parent_->TimerStart();
  }
 protected:
  FirmwareAppComponent *parent_;
};

template<typename... Ts>
class TimerStopAction : public Action<Ts...> {
 public:
  explicit TimerStopAction(FirmwareAppComponent *parent) : parent_(parent) {}
  void play(const Ts &...x) override {
    (void) sizeof...(x);
    this->parent_->TimerStop();
  }
 protected:
  FirmwareAppComponent *parent_;
};

template<typename... Ts>
class TimerResetAction : public Action<Ts...> {
 public:
  explicit TimerResetAction(FirmwareAppComponent *parent) : parent_(parent) {}
  void play(const Ts &...x) override {
    (void) sizeof...(x);
    this->parent_->TimerReset();
  }
 protected:
  FirmwareAppComponent *parent_;
};

template<typename... Ts>
class RebootAction : public Action<Ts...> {
 public:
  explicit RebootAction(FirmwareAppComponent *parent) : parent_(parent) {}

  void play(const Ts &...x) override {
    (void) sizeof...(x);
    this->parent_->RequestReboot();
  }

 protected:
  FirmwareAppComponent *parent_;
};

template<typename... Ts>
class BeginFirmwareUpdateAction : public Action<Ts...> {
 public:
  explicit BeginFirmwareUpdateAction(FirmwareAppComponent *parent)
      : parent_(parent) {}

  void play(const Ts &...x) override {
    (void) sizeof...(x);
    this->parent_->BeginFirmwareUpdate();
  }

 protected:
  FirmwareAppComponent *parent_;
};

template<typename... Ts>
class ClearOverlayQueueAction : public Action<Ts...> {
 public:
  explicit ClearOverlayQueueAction(FirmwareAppComponent *parent) : parent_(parent) {}

  void play(const Ts &...x) override {
    (void) sizeof...(x);
    this->parent_->ClearOverlayQueue();
  }

 protected:
  FirmwareAppComponent *parent_;
};

}  // namespace esphome::pixoo64
