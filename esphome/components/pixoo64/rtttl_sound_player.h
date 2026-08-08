#pragma once

#include "esphome/components/rtttl/rtttl.h"
#include "esphome/components/switch/switch.h"
#include "esphome/core/component.h"
#include "pixoo_sound.h"

namespace esphome::pixoo64::adapters {

// The single concrete pixoo::SoundPlayer. This is the only firmware code that
// knows the tone backend (ESPHome's rtttl over the GPIO23 LEDC output): it owns
// the Sound->melody table and per-sound gain, and short-circuits when the
// persisted "Sounds enabled" switch is off. Other components depend only on the
// abstract pixoo::SoundPlayer, never on this class or on rtttl.
class RtttlSoundPlayer : public Component, public pixoo::SoundPlayer {
 public:
  void set_rtttl(rtttl::Rtttl *rtttl) { this->rtttl_ = rtttl; }
  // Optional mute gate; when set and off, Play() does nothing.
  void set_enable_switch(switch_::Switch *sw) { this->enable_switch_ = sw; }

  void Play(pixoo::Sound sound) override;
  void Stop() override;
  void on_shutdown() override;

 protected:
  rtttl::Rtttl *rtttl_{nullptr};
  switch_::Switch *enable_switch_{nullptr};
};

}  // namespace esphome::pixoo64::adapters
