#pragma once

#include <optional>
#include <string>

#include "esphome/components/light/light_state.h"
#include "esphome/components/select/select.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/core/application.h"
#include "esphome/core/automation.h"
#include "esphome/core/component.h"
#include "esphome/core/preferences.h"
#include "firmware_app.h"

namespace esphome::pixoo64 {

class FirmwareAppComponent final : public PollingComponent,
                                   public pixoo::LightStateSink,
                                   public pixoo::SystemPort,
                                   public pixoo::FrameMetricsPort {
 public:
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
  void SyncLightFromEntity();
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

  void Publish(pixoo::LightState state) override;
  void FactoryReset() override;
  void BeginRegularFrame() override;
  void EndRegularFrame() override;

 protected:
  static bool ParseSound_(const std::string &name, pixoo::Sound *sound);
  pixoo::LightState ReadLight_() const;
  void PublishFrameMetrics_(uint32_t now_ms);

  pixoo::PanelPort *panel_{nullptr};
  Component *panel_component_{nullptr};
  pixoo::RenderPort *renderer_{nullptr};
  pixoo::SoundPlayer *sound_player_{nullptr};
  pixoo::MicrophonePort *microphone_{nullptr};
  light::LightState *light_{nullptr};
  select::Select *dashboard_select_{nullptr};
  sensor::Sensor *frame_average_sensor_{nullptr};
  sensor::Sensor *frame_max_sensor_{nullptr};
  sensor::Sensor *rendered_fps_sensor_{nullptr};
  uint32_t frame_metrics_window_ms_{0};
  uint32_t frame_started_us_{0};
  bool frame_timing_active_{false};
  pixoo::FrameMetricsWindow frame_metrics_window_;
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
