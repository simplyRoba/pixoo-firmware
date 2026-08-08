#pragma once

namespace pixoo {

// The closed vocabulary of sounds the firmware can play. Callers name a sound;
// they never supply a melody or reference the tone backend. Adding a sound is a
// new enumerator here plus a mapping in the concrete player.
// Sound names describe the noise, not any caller intent. They are independent
// of notification severity: any sound can be paired with any banner.
enum class Sound {
  kBoot,     // startup chime (very soft)
  kChirp,    // quick rising triad
  kSuccess,  // rising flourish up to a high note
  kPling1,   // two-note down-step
  kPling2,   // two-note down-step, higher
  kPling3,   // two-note octave drop, mid
  kPling4,   // two-note octave drop, high
  kAlarm1,   // fast repeated note
  kAlarm2,   // mid repeated note
  kAlarm3,   // sparse repeated note
};

// Abstract sound output. A component that wants a beep holds a SoundPlayer* and
// calls Play(); it stays independent of the buzzer and the tone backend. The
// single concrete implementation is the pixoo64 component's RtttlSoundPlayer.
class SoundPlayer {
 public:
  virtual ~SoundPlayer() = default;

  // Play a named sound. Latest wins: a Play() while a sound is playing replaces
  // it. A muted player does nothing.
  virtual void Play(Sound sound) = 0;

  // Stop any sound currently playing.
  virtual void Stop() = 0;
};

}  // namespace pixoo
