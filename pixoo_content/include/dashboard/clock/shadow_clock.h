#pragma once

#include <cstdint>

namespace pixoo::clock {

// Degrees clockwise from 12 o'clock, normalized to [0, 360).
struct ShadowHandAngles {
  float hour{0.0f};
  float minute{0.0f};
  float second{0.0f};
};

struct ShadowHandLevels {
  float hour{0.0f};
  float minute{0.0f};
  float second{0.0f};
};

// Continuously sweeping hands with independent light ignition. Repeated civil
// timestamps advance by at most one inferred second before the hands freeze.
class ShadowClockModel {
 public:
  static constexpr uint32_t kLoadMs = 1500;

  void StartLoad(uint32_t now_ms);
  void Clear();
  void Update(int hour, int minute, int second, uint32_t now_ms);

  bool HasTime() const { return this->has_time_; }
  // Maximum hand level, not a global fade applied to every hand.
  float Level() const;
  ShadowHandLevels Levels() const { return this->levels_; }
  ShadowHandAngles Angles() const { return this->angles_; }

 private:
  ShadowHandAngles angles_{};
  ShadowHandLevels levels_{};
  uint32_t load_started_ms_{0};
  uint32_t second_started_ms_{0};
  int civil_second_{-1};
  bool has_time_{false};
  bool loading_{false};
};

}  // namespace pixoo::clock
