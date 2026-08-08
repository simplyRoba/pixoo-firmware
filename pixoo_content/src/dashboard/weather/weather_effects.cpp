#include "weather_effects.h"

#include <cmath>

namespace pixoo {

WeatherEffect WeatherEffectFor(WeatherCondition condition) {
  switch (condition) {
    case WeatherCondition::UNKNOWN:
    case WeatherCondition::SUNNY:
      return {};
    case WeatherCondition::PARTLYCLOUDY:
      return {CloudCover::PARTLY_CLOUDY, PrecipitationKind::NONE,
              PrecipitationIntensity::NONE, false, false, false};
    case WeatherCondition::CLOUDY:
      return {CloudCover::CLOUDY, PrecipitationKind::NONE,
              PrecipitationIntensity::NONE, false, false, false};
    case WeatherCondition::FOG:
      return {CloudCover::NONE, PrecipitationKind::NONE,
              PrecipitationIntensity::NONE, true, false, false};
    case WeatherCondition::DRIZZLE:
      return {CloudCover::OVERCAST, PrecipitationKind::DRIZZLE,
              PrecipitationIntensity::SPARSE, false, false, false};
    case WeatherCondition::FREEZING_DRIZZLE:
      return {CloudCover::OVERCAST, PrecipitationKind::DRIZZLE,
              PrecipitationIntensity::SPARSE, false, true, false};
    case WeatherCondition::RAINY:
      return {CloudCover::OVERCAST, PrecipitationKind::RAIN,
              PrecipitationIntensity::MEDIUM, false, false, false};
    case WeatherCondition::POURING:
      return {CloudCover::OVERCAST, PrecipitationKind::RAIN,
              PrecipitationIntensity::HEAVY, false, false, false};
    case WeatherCondition::FREEZING_RAIN:
      return {CloudCover::OVERCAST, PrecipitationKind::RAIN,
              PrecipitationIntensity::MEDIUM, false, true, false};
    case WeatherCondition::SNOWY:
      return {CloudCover::OVERCAST, PrecipitationKind::SNOW,
              PrecipitationIntensity::MEDIUM, false, false, false};
    case WeatherCondition::SNOW_GRAINS:
      return {CloudCover::OVERCAST, PrecipitationKind::SNOW_GRAINS,
              PrecipitationIntensity::MEDIUM, false, false, false};
    case WeatherCondition::THUNDERSTORM:
      return {CloudCover::OVERCAST, PrecipitationKind::RAIN,
              PrecipitationIntensity::HEAVY, false, false, true};
    case WeatherCondition::HAIL_THUNDERSTORM:
      return {CloudCover::OVERCAST, PrecipitationKind::HAIL,
              PrecipitationIntensity::HEAVY, false, false, true};
  }
  return {};
}

float CloudCoverLevel(CloudCover cover) {
  switch (cover) {
    case CloudCover::NONE:
      return 0.0f;
    case CloudCover::PARTLY_CLOUDY:
      return 1.0f;
    case CloudCover::CLOUDY:
      return 2.0f;
    case CloudCover::OVERCAST:
      return 3.0f;
  }
  return 0.0f;
}

namespace {

float SmoothBlend(float t) {
  const float c = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
  return c * c * (3.0f - 2.0f * c);
}

// Traits that differ between two conditions. Each one is a separate visual
// change the viewer has to follow, so the count sets the handoff duration.
uint8_t DifferingTraitCount(const WeatherEffect &from, const WeatherEffect &to) {
  uint8_t count = 0;
  if (from.clouds != to.clouds)
    count++;
  if (from.precipitation != to.precipitation)
    count++;
  if (from.intensity != to.intensity)
    count++;
  if (from.fog != to.fog)
    count++;
  if (from.freezing != to.freezing)
    count++;
  if (from.storm != to.storm)
    count++;
  return count;
}

}  // namespace

uint64_t WeatherTransitionDurationMs(WeatherCondition from,
                                     WeatherCondition to) {
  constexpr uint8_t kTraitCount = 6u;
  const uint8_t differing =
      DifferingTraitCount(WeatherEffectFor(from), WeatherEffectFor(to));
  if (differing <= 1u)
    return kWeatherTransitionMinDurationMs;
  const uint64_t span =
      kWeatherTransitionMaxDurationMs - kWeatherTransitionMinDurationMs;
  return kWeatherTransitionMinDurationMs +
         span * (differing - 1u) / (kTraitCount - 1u);
}

WeatherEffectMix MixWeatherEffects(const WeatherEffect &from,
                                   const WeatherEffect &to, float blend) {
  const float t = SmoothBlend(blend);
  WeatherEffectMix mix;
  mix.cloud_cover = CloudCoverLevel(from.clouds) +
                    (CloudCoverLevel(to.clouds) - CloudCoverLevel(from.clouds)) * t;
  mix.fog = (from.fog ? 1.0f - t : 0.0f) + (to.fog ? t : 0.0f);
  mix.freezing = (from.freezing ? 1.0f - t : 0.0f) + (to.freezing ? t : 0.0f);
  mix.storm = (from.storm ? 1.0f - t : 0.0f) + (to.storm ? t : 0.0f);

  // Fields of different kind, cadence, or state cannot share particles, so
  // each retires or arrives on its own while the other runs undisturbed.
  const bool same_field = from.precipitation == to.precipitation &&
                          from.intensity == to.intensity &&
                          from.freezing == to.freezing;
  const auto add = [&mix](PrecipitationKind kind,
                          PrecipitationIntensity intensity, bool freezing,
                          float weight) {
    if (kind == PrecipitationKind::NONE || weight <= 0.0f ||
        mix.precipitation_count >= WeatherEffectMix::kMaxPrecipitationLayers)
      return;
    mix.precipitation[mix.precipitation_count++] = {kind, intensity, freezing,
                                                    weight};
  };
  // An unchanged field is one continuous set of particles at full weight.
  if (same_field) {
    add(to.precipitation, to.intensity, to.freezing, 1.0f);
    return mix;
  }
  add(from.precipitation, from.intensity, from.freezing, 1.0f - t);
  add(to.precipitation, to.intensity, to.freezing, t);
  return mix;
}

WeatherEffectMix WeatherTransitionState::Mix() const {
  return MixWeatherEffects(this->outgoing_effect_, this->incoming_effect_,
                           this->blend_);
}

void WeatherTransitionState::Update(WeatherCondition condition,
                                    uint64_t elapsed_ms) {
  this->latest_observed_condition_ = condition;
  if (!this->initialized_) {
    this->outgoing_condition_ = condition;
    this->outgoing_effect_ = WeatherEffectFor(condition);
    this->incoming_condition_ = condition;
    this->incoming_effect_ = this->outgoing_effect_;
    this->initialized_ = true;
    return;
  }

  this->SettleThrough(elapsed_ms);
  if (this->transitioning_ ||
      this->latest_observed_condition_ == this->incoming_condition_)
    return;

  this->outgoing_condition_ = this->incoming_condition_;
  this->outgoing_effect_ = this->incoming_effect_;
  this->incoming_condition_ = this->latest_observed_condition_;
  this->incoming_effect_ = WeatherEffectFor(this->incoming_condition_);
  this->duration_ms_ = WeatherTransitionDurationMs(this->outgoing_condition_,
                                                   this->incoming_condition_);
  this->transition_started_ms_ = elapsed_ms;
  this->blend_ = 0.0f;
  this->transitioning_ = true;
}

void WeatherTransitionState::SettleThrough(uint64_t elapsed_ms) {
  if (!this->transitioning_)
    return;

  const uint64_t transition_elapsed_ms =
      elapsed_ms - this->transition_started_ms_;
  if (transition_elapsed_ms >= this->duration_ms_) {
    this->outgoing_condition_ = this->incoming_condition_;
    this->outgoing_effect_ = this->incoming_effect_;
    this->blend_ = 1.0f;
    this->transitioning_ = false;
    return;
  }

  this->blend_ = static_cast<float>(transition_elapsed_ms) /
                 static_cast<float>(this->duration_ms_);
}

namespace {

constexpr uint32_t kLightningSeed = 0x4c495447u;
constexpr uint32_t kLightningSlots = 16u;
constexpr uint32_t kLightningRiseMs = 80u;
constexpr uint32_t kLightningFallMs = 220u;

constexpr uint32_t LightningIntervalMs(uint32_t slot) {
  return kLightningMinIntervalMs +
         WeatherHash(kLightningSeed + slot) %
             (kLightningMaxIntervalMs - kLightningMinIntervalMs + 1u);
}

constexpr uint64_t LightningCycleMs() {
  uint64_t duration = 0;
  for (uint32_t slot = 0; slot < kLightningSlots; slot++)
    duration += LightningIntervalMs(slot);
  return duration;
}

constexpr uint64_t kLightningCycleMs = LightningCycleMs();

uint8_t LightningIntensity(uint32_t offset_ms) {
  if (offset_ms < kLightningRiseMs)
    return static_cast<uint8_t>(
        32u + offset_ms * (kLightningMaxIntensity - 32u) / kLightningRiseMs);
  if (offset_ms < kLightningRiseMs + kLightningFallMs) {
    const uint32_t fall_offset = offset_ms - kLightningRiseMs;
    return static_cast<uint8_t>(
        kLightningMaxIntensity - fall_offset * (kLightningMaxIntensity - 24u) /
                                     kLightningFallMs);
  }
  if (offset_ms < kLightningDurationMs) {
    const uint32_t afterglow_offset = offset_ms - kLightningRiseMs - kLightningFallMs;
    const uint32_t afterglow_ms = kLightningDurationMs - kLightningRiseMs - kLightningFallMs;
    return static_cast<uint8_t>(24u - afterglow_offset * 24u / afterglow_ms);
  }
  return 0u;
}

}  // namespace

PrecipitationKinematics PrecipitationKinematicsFor(
    PrecipitationKind kind, PrecipitationIntensity intensity) {
  switch (kind) {
    case PrecipitationKind::DRIZZLE:
      return {6144u, 1};
    case PrecipitationKind::RAIN:
      return {intensity == PrecipitationIntensity::HEAVY ? 2048u : 3072u,
              3};
    case PrecipitationKind::SNOW:
      return {11200u, 3};
    case PrecipitationKind::SNOW_GRAINS:
      return {3200u, 1};
    case PrecipitationKind::HAIL:
      return {1400u, 1};
    case PrecipitationKind::NONE:
      return {};
  }
  return {};
}

LightningState LightningAt(bool storm, uint64_t elapsed_ms) {
  if (!storm)
    return {};

  const uint64_t cycle = elapsed_ms / kLightningCycleMs;
  const uint32_t phase_ms = static_cast<uint32_t>(elapsed_ms % kLightningCycleMs);
  uint32_t event_start_ms = 0;
  for (uint32_t slot = 0; slot < kLightningSlots; slot++) {
    if (phase_ms >= event_start_ms &&
        phase_ms < event_start_ms + kLightningDurationMs) {
      const uint32_t event_index =
          static_cast<uint32_t>(cycle * kLightningSlots + slot);
      return {LightningIntensity(phase_ms - event_start_ms),
              WeatherHash(kLightningSeed ^ event_index), event_index};
    }
    event_start_ms += LightningIntervalMs(slot);
  }
  return {};
}

uint32_t ElapsedMillis(uint32_t now_ms, uint32_t then_ms) {
  return now_ms - then_ms;
}

void WeatherAnimationClock::Reset(uint32_t now_ms) {
  this->last_ms_ = now_ms;
  this->elapsed_ms_ = 0;
  this->initialized_ = true;
}

void WeatherAnimationClock::Resume(uint32_t now_ms) {
  if (!this->initialized_) {
    this->elapsed_ms_ = 0;
    this->initialized_ = true;
  }
  this->last_ms_ = now_ms;
}

void WeatherAnimationClock::Tick(uint32_t now_ms) {
  if (!this->initialized_) {
    this->Resume(now_ms);
    return;
  }
  this->elapsed_ms_ += ElapsedMillis(now_ms, this->last_ms_);
  this->last_ms_ = now_ms;
}

uint32_t AnimationPhase(uint64_t elapsed_ms, uint32_t period_ms,
                        uint32_t seed) {
  if (period_ms == 0)
    return 0;
  return static_cast<uint32_t>((elapsed_ms + WeatherHash(seed)) % period_ms);
}

namespace {

// Value noise: hash the integer lattice and smoothly interpolate. Cheaper than
// gradient noise and sufficient for a soft, shapeless medium like fog.
float LatticeValue(int32_t xi, int32_t yi, uint32_t repeat_width,
                   uint32_t seed) {
  const int32_t w = static_cast<int32_t>(repeat_width);
  const int32_t wrapped = ((xi % w) + w) % w;
  const uint32_t h = WeatherHash(static_cast<uint32_t>(wrapped) * 0x9e3779b9u ^
                                 static_cast<uint32_t>(yi) * 0x85ebca6bu ^
                                 seed);
  return static_cast<float>(h & 0xffffu) / 65535.0f;
}

float SmoothStep(float t) { return t * t * (3.0f - 2.0f * t); }

// One octave sampled on a lattice of `cells` columns across repeat_width,
// scrolling sideways by `shift` cells. Wrapping on the cell index is what
// makes the field seamless however far it drifts.
float FogOctave(float x, float y, float shift, uint32_t cells, float cell_h,
                uint32_t repeat_width, uint32_t seed) {
  const float cell_w = static_cast<float>(repeat_width) / cells;
  const float fx = x / cell_w + shift;
  const float fy = y / cell_h;
  const int32_t x0 = static_cast<int32_t>(std::floor(fx));
  const int32_t y0 = static_cast<int32_t>(std::floor(fy));
  const float tx = SmoothStep(fx - static_cast<float>(x0));
  const float ty = SmoothStep(fy - static_cast<float>(y0));
  const float v00 = LatticeValue(x0, y0, cells, seed);
  const float v10 = LatticeValue(x0 + 1, y0, cells, seed);
  const float v01 = LatticeValue(x0, y0 + 1, cells, seed);
  const float v11 = LatticeValue(x0 + 1, y0 + 1, cells, seed);
  const float top = v00 + (v10 - v00) * tx;
  const float bottom = v01 + (v11 - v01) * tx;
  return top + (bottom - top) * ty;
}

}  // namespace

float FogDensityAt(float x, float y, uint64_t elapsed_ms,
                   uint32_t repeat_width) {
  if (repeat_width == 0u)
    return 0.0f;
  // Two banks drifting at different rates, so the fog churns instead of
  // sliding rigidly. Periods are the time to travel one full cell.
  const float coarse_shift =
      static_cast<float>(elapsed_ms % 47000u) / 47000.0f;
  const float fine_shift =
      static_cast<float>(elapsed_ms % 29000u) / 29000.0f;
  const float coarse =
      FogOctave(x, y, coarse_shift * 4.0f, 4u, 11.0f, repeat_width, 0x51u);
  const float fine =
      FogOctave(x, y, fine_shift * 8.0f, 8u, 7.0f, repeat_width, 0xb7u);
  const float v = 0.62f * coarse + 0.38f * fine;
  return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}

}  // namespace pixoo
