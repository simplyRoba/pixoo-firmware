#include "rtttl_sound_player.h"

#include <string>

#include "esphome/core/log.h"

namespace esphome::pixoo64::adapters {
namespace {

static const char *const TAG = "pixoo64.sound";

// One entry per pixoo::Sound. gain is the rtttl playback level (0..1); the boot
// chime is deliberately very soft. Melodies are short RTTTL (Nokia ringtone)
// strings so a sound never lingers over the content.
struct SoundDef {
  float gain;
  const char *rtttl;
};

const SoundDef &sound_def(pixoo::Sound sound) {
  static const SoundDef kBoot{
      0.12f, "boot:d=16,o=6,b=200:c,e,g"};
  static const SoundDef kChirp{
      0.35f, "chirp:d=16,o=6,b=300:c,e,g"};
  static const SoundDef kSuccess{
      0.35f, "success:d=16,o=6,b=220:c,e,g,2c7"};
  static const SoundDef kPling1{
      0.35f, "pling:d=16,o=6,b=140:e6,32p,d6"};
  static const SoundDef kPling2{
      0.35f, "pling2:d=16,o=7,b=140:f#7,32p,e7"};
  static const SoundDef kPling3{
      0.35f, "pling3:d=16,o=5,b=120:g6,32p,g5"};
  static const SoundDef kPling4{
      0.35f, "pling4:d=16,o=5,b=100:a6,32p,a5"};
  static const SoundDef kAlarm1{
      0.55f, "alarm:d=8,o=5,b=160:a,p,a,p,a,p,a,p"};
  static const SoundDef kAlarm2{
      0.55f, "alarm2:d=4,o=5,b=150:g#,8p,g#,8p,g#,8p,g#"};
  static const SoundDef kAlarm3{
      0.55f, "alarm4:d=4,o=6,b=100:c,p,c,p,c,p,c"};

  switch (sound) {
    case pixoo::Sound::kBoot:
      return kBoot;
    case pixoo::Sound::kChirp:
      return kChirp;
    case pixoo::Sound::kSuccess:
      return kSuccess;
    case pixoo::Sound::kPling1:
      return kPling1;
    case pixoo::Sound::kPling2:
      return kPling2;
    case pixoo::Sound::kPling3:
      return kPling3;
    case pixoo::Sound::kPling4:
      return kPling4;
    case pixoo::Sound::kAlarm1:
      return kAlarm1;
    case pixoo::Sound::kAlarm2:
      return kAlarm2;
    case pixoo::Sound::kAlarm3:
      return kAlarm3;
  }
  return kPling1;
}

}  // namespace

void RtttlSoundPlayer::Play(pixoo::Sound sound) {
  if (this->rtttl_ == nullptr)
    return;
  if (this->enable_switch_ != nullptr && !this->enable_switch_->state) {
    ESP_LOGD(TAG, "sound muted; skipping");
    return;
  }
  const SoundDef &def = sound_def(sound);
  // Latest wins: stop any tone in progress before starting the next.
  if (this->rtttl_->is_playing())
    this->rtttl_->stop();
  this->rtttl_->set_gain(def.gain);
  this->rtttl_->play(std::string(def.rtttl));
}

void RtttlSoundPlayer::Stop() {
  if (this->rtttl_ != nullptr && this->rtttl_->is_playing())
    this->rtttl_->stop();
}

void RtttlSoundPlayer::on_shutdown() {
  this->Stop();
}

}  // namespace esphome::pixoo64::adapters
