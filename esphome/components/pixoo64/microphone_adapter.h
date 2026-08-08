#pragma once

#include <cstddef>
#include <cstdint>

#include "esphome/components/i2s_audio/microphone/i2s_audio_microphone.h"
#include "esphome/components/switch/switch.h"
#include "esphome/core/component.h"
#include "equalizer_processor.h"
#include "firmware_app.h"

namespace esphome::pixoo64::adapters {

// Owns the ESPHome I2S microphone lifecycle and hands normalized spectrum levels
// to the renderer through the framework-independent EqualizerLevelsSink port.
class MicrophoneAdapter final : public PollingComponent,
                                public pixoo::MicrophonePort {
 public:
  void setup() override;
  void update() override;
  void on_shutdown() override;
  bool teardown() override;
  void on_powerdown() override;
  void dump_config() override;
  float get_setup_priority() const override {
    return setup_priority::DATA - 1.0f;
  }

  void set_microphone(i2s_audio::I2SAudioMicrophone *microphone) {
    this->microphone_ = microphone;
  }
  void set_enable_switch(switch_::Switch *enable_switch) {
    this->enable_switch_ = enable_switch;
  }
  void set_levels_sink(pixoo::EqualizerLevelsSink *sink) {
    this->levels_sink_ = sink;
  }

  void SetEnabled(bool enabled) override;

 protected:
  static constexpr uint32_t kStopFallbackMs = 1000;

  void ServiceMicrophone_();
  void SetEnableSwitch_(bool enabled);

  i2s_audio::I2SAudioMicrophone *microphone_{nullptr};
  switch_::Switch *enable_switch_{nullptr};
  pixoo::EqualizerLevelsSink *levels_sink_{nullptr};
  pixoo::EqualizerProcessor processor_;
  bool configured_{false};
  bool microphone_requested_{false};
  bool microphone_stop_pending_{false};
  bool microphone_stop_issued_{false};
  bool microphone_enable_on_{false};
  uint32_t microphone_stop_pending_started_ms_{0};
};

}  // namespace esphome::pixoo64::adapters
