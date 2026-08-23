#pragma once

#include <cmath>

#include "app_state.h"

namespace pixoo {

// Suppresses the ESPHome state callback produced by an application-originated
// light publication. A mismatched callback is external user input and consumes
// the pending expectation without being suppressed.
class LightStatePublicationGuard {
 public:
  void Expect(LightState state) {
    this->expected_ = state;
    this->pending_ = true;
  }

  bool ConsumeIfExpected(LightState state) {
    if (!this->pending_)
      return false;
    this->pending_ = false;
    return state.on == this->expected_.on &&
           std::fabs(state.brightness - this->expected_.brightness) <= 1.0e-6f;
  }

  bool pending() const { return this->pending_; }

 private:
  LightState expected_{};
  bool pending_{false};
};

}  // namespace pixoo
