#pragma once

#include <cstdint>

namespace pixoo {

// Main-task-owned refresh policy for a location-scoped weather cache. It has no
// framework, network, or task dependencies; callers supply the uint32_t millis
// clock and run all mutations on one task.
class WeatherRefreshPolicy {
 public:
  static constexpr uint32_t kFailureBackoffMs = 30000;

  enum class Completion {
    kIgnored,
    kSuccess,
    kFailure,
  };

  // Starts a request when there is no request in flight and the current cache
  // is absent, stale, explicitly invalidated, or past a failed-request backoff.
  // Returns the location generation to tag onto the immutable request.
  bool BeginRequest(uint32_t now, uint32_t freshness_ms,
                    uint32_t *location_generation);

  // Completes the one active request. A completion for an old location is
  // ignored and cannot make that location's data visible again.
  Completion Complete(uint32_t request_generation, bool succeeded,
                      uint32_t now);

  // Hides data from the old location immediately and forces the next request.
  void Invalidate();

  // True only when a successful snapshot belongs to the current location.
  bool HasCurrentData() const;
  uint32_t location_generation() const { return location_generation_; }

 private:
  static bool Elapsed_(uint32_t now, uint32_t then, uint32_t interval) {
    return now - then >= interval;
  }

  uint32_t location_generation_{0};
  uint32_t active_generation_{0};
  uint32_t success_generation_{0};
  uint32_t failure_generation_{0};
  uint32_t last_success_ms_{0};
  uint32_t last_failure_ms_{0};
  bool in_flight_{false};
  bool force_refresh_{true};
  bool has_success_{false};
  bool has_failure_{false};
};

}  // namespace pixoo
