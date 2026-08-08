#include "refresh_policy.h"

namespace pixoo {

bool WeatherRefreshPolicy::BeginRequest(uint32_t now, uint32_t freshness_ms,
                                        uint32_t *location_generation) {
  if (this->in_flight_)
    return false;

  const bool has_current_success =
      this->has_success_ &&
      this->success_generation_ == this->location_generation_;
  const bool failure_backoff_active =
      this->has_failure_ &&
      this->failure_generation_ == this->location_generation_ &&
      !Elapsed_(now, this->last_failure_ms_, kFailureBackoffMs);
  const bool fresh = has_current_success &&
                     !Elapsed_(now, this->last_success_ms_, freshness_ms);
  if (!this->force_refresh_ && (failure_backoff_active || fresh))
    return false;

  this->in_flight_ = true;
  this->active_generation_ = this->location_generation_;
  this->force_refresh_ = false;
  if (location_generation != nullptr)
    *location_generation = this->active_generation_;
  return true;
}

WeatherRefreshPolicy::Completion WeatherRefreshPolicy::Complete(
    uint32_t request_generation, bool succeeded, uint32_t now) {
  if (!this->in_flight_ || request_generation != this->active_generation_)
    return Completion::kIgnored;

  this->in_flight_ = false;
  if (request_generation != this->location_generation_)
    return Completion::kIgnored;

  if (succeeded) {
    this->has_success_ = true;
    this->success_generation_ = request_generation;
    this->last_success_ms_ = now;
    this->has_failure_ = false;
    return Completion::kSuccess;
  }

  this->has_failure_ = true;
  this->failure_generation_ = request_generation;
  this->last_failure_ms_ = now;
  return Completion::kFailure;
}

void WeatherRefreshPolicy::Invalidate() {
  ++this->location_generation_;
  this->force_refresh_ = true;
}

bool WeatherRefreshPolicy::HasCurrentData() const {
  return this->has_success_ &&
         this->success_generation_ == this->location_generation_;
}

}  // namespace pixoo
