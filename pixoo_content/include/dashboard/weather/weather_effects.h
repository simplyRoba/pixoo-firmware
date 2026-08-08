#pragma once

#include <cstdint>

#include "weather_data.h"

namespace pixoo {

// Cloud cover and precipitation are presentation-neutral weather traits. A
// dashboard selects its own pixels from these traits rather than duplicating
// condition mappings.
enum class CloudCover : uint8_t {
  NONE,
  PARTLY_CLOUDY,
  CLOUDY,
  OVERCAST,
};

enum class PrecipitationKind : uint8_t {
  NONE,
  DRIZZLE,
  RAIN,
  SNOW,
  SNOW_GRAINS,
  HAIL,
};

// Intensity is zero for no precipitation, then sparse, medium, or heavy.
enum class PrecipitationIntensity : uint8_t {
  NONE,
  SPARSE,
  MEDIUM,
  HEAVY,
};

struct WeatherEffect {
  CloudCover clouds{CloudCover::NONE};
  PrecipitationKind precipitation{PrecipitationKind::NONE};
  PrecipitationIntensity intensity{PrecipitationIntensity::NONE};
  bool fog{false};
  bool freezing{false};
  bool storm{false};
};

// One field of falling particles and how much of the scene it holds. A second
// field exists only while one kind or cadence hands over to another; each keeps
// its own fall cadence, so no particle changes speed or jumps position.
struct PrecipitationLayer {
  PrecipitationKind kind{PrecipitationKind::NONE};
  PrecipitationIntensity intensity{PrecipitationIntensity::NONE};
  bool freezing{false};
  float weight{0.0f};
};

// A condition resolved to continuous scene quantities. Traits that differ by
// degree are a single moving value, so a renderer draws one scene state per
// frame instead of compositing two complete conditions.
struct WeatherEffectMix {
  // Continuous cover level: 0 clear, 1 partly cloudy, 2 cloudy, 3 overcast.
  float cloud_cover{0.0f};
  // At most one retiring and one arriving field, whose weights sum to one.
  static constexpr uint8_t kMaxPrecipitationLayers = 2u;
  PrecipitationLayer precipitation[kMaxPrecipitationLayers]{};
  uint8_t precipitation_count{0};
  float fog{0.0f};
  float freezing{0.0f};
  float storm{0.0f};
};

float CloudCoverLevel(CloudCover cover);

// Scene quantities `blend` of the way from one effect to the other. Identical
// precipitation resolves to one full-weight layer; otherwise the outgoing and
// incoming fields split the weight.
WeatherEffectMix MixWeatherEffects(const WeatherEffect &from,
                                   const WeatherEffect &to, float blend);

// Fall cadence and maximum sideways drift for stateless precipitation.
// Individual render layers may vary these values to convey depth. Heavy rain
// uses its own cadence through PrecipitationKinematicsFor().
struct PrecipitationKinematics {
  uint32_t fall_period_ms{0};
  int8_t drift_px{0};
};

// Fixed hashes and phases provide repeatable, stateless particle placement.
constexpr uint32_t WeatherHash(uint32_t value) {
  value ^= value >> 16;
  value *= 0x7feb352du;
  value ^= value >> 15;
  value *= 0x846ca68bu;
  value ^= value >> 16;
  return value;
}

// A storm schedules isolated lightning events from a fixed hash sequence.
// Intensity is 0..kLightningMaxIntensity and zero outside an event.
constexpr uint32_t kLightningMinIntervalMs = 5000u;
constexpr uint32_t kLightningMaxIntervalMs = 12000u;
constexpr uint32_t kLightningDurationMs = 520u;
constexpr uint8_t kLightningMaxIntensity = 128u;

struct LightningState {
  uint8_t intensity{0};
  uint32_t bolt_seed{0};
  uint32_t event_index{0};
};

WeatherEffect WeatherEffectFor(WeatherCondition condition);
// Only rain resolves a different cadence per intensity; the other kinds have
// one cadence and ignore it.
PrecipitationKinematics PrecipitationKinematicsFor(
    PrecipitationKind kind, PrecipitationIntensity intensity);

// Conditions cross-fade over a caller-driven animation interval that grows
// with the number of traits that differ, so a single-trait change is brief
// while a wholesale change has room to resolve.
constexpr uint64_t kWeatherTransitionMinDurationMs = 1200u;
constexpr uint64_t kWeatherTransitionMaxDurationMs = 3000u;
uint64_t WeatherTransitionDurationMs(WeatherCondition from,
                                     WeatherCondition to);

// Retained condition/effect state for a weather cross-fade. Update() must be
// called with the current condition and monotonic animation elapsed time.
class WeatherTransitionState {
 public:
  // The first condition is settled immediately. A condition change starts at
  // blend zero. While a transition is active, it continues unchanged and each
  // observation replaces the deferred target. After it settles, the latest
  // target starts from the settled incoming condition at blend zero.
  void Update(WeatherCondition condition, uint64_t elapsed_ms);

  bool initialized() const { return this->initialized_; }
  bool transitioning() const { return this->transitioning_; }
  float blend() const { return this->blend_; }
  WeatherCondition outgoing_condition() const {
    return this->outgoing_condition_;
  }
  const WeatherEffect &outgoing_effect() const {
    return this->outgoing_effect_;
  }
  WeatherCondition incoming_condition() const {
    return this->incoming_condition_;
  }
  const WeatherEffect &incoming_effect() const {
    return this->incoming_effect_;
  }
  uint64_t duration_ms() const { return this->duration_ms_; }
  // Scene quantities at the current point of the handoff.
  WeatherEffectMix Mix() const;

 private:
  void SettleThrough(uint64_t elapsed_ms);

  WeatherCondition outgoing_condition_{WeatherCondition::UNKNOWN};
  WeatherEffect outgoing_effect_{};
  WeatherCondition incoming_condition_{WeatherCondition::UNKNOWN};
  WeatherCondition latest_observed_condition_{WeatherCondition::UNKNOWN};
  WeatherEffect incoming_effect_{};
  uint64_t transition_started_ms_{0};
  uint64_t duration_ms_{kWeatherTransitionMinDurationMs};
  float blend_{1.0f};
  bool initialized_{false};
  bool transitioning_{false};
};

// Returns an inactive state unless a storm is present. One timeline serves the
// whole scene, so a handoff never places two bolts at once. The result is a
// pure function of the wrap-safe animation timeline and fixed hashes, so no
// event state is retained by a renderer.
LightningState LightningAt(bool storm, uint64_t elapsed_ms);

// Millis timestamps are unsigned counters. Subtraction yields the elapsed
// interval across one uint32_t wrap, provided callers observe it at least once
// per wrap.
uint32_t ElapsedMillis(uint32_t now_ms, uint32_t then_ms);

// A monotonic animation duration assembled from successive millis timestamps.
// It has no particle state; it only preserves a continuous time base through
// the millis wrap.
class WeatherAnimationClock {
 public:
  void Reset(uint32_t now_ms);

  // Sets the raw millis baseline without changing the animation phase. On the
  // first call it initializes elapsed time to zero. Use after a dashboard is
  // shown so hidden wall time is not added to the animation timeline.
  void Resume(uint32_t now_ms);

  void Tick(uint32_t now_ms);
  uint64_t elapsed_ms() const { return this->elapsed_ms_; }

 private:
  uint32_t last_ms_{0};
  uint64_t elapsed_ms_{0};
  bool initialized_{false};
};

uint32_t AnimationPhase(uint64_t elapsed_ms, uint32_t period_ms,
                        uint32_t seed);

// Fog density in [0,1] at a point, as a smooth value-noise field that drifts
// sideways over time. Two octaves at different speeds keep the bank from
// sliding as one rigid sheet. The field wraps over repeat_width, so it is
// seamless however long it scrolls, and it is a pure function of its inputs:
// no field state is retained by a renderer.
float FogDensityAt(float x, float y, uint64_t elapsed_ms,
                   uint32_t repeat_width);

}  // namespace pixoo
