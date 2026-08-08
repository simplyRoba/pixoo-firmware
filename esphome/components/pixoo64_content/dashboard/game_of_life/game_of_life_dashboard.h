#pragma once

#include <cstdint>

#include "dashboard/dashboard.h"
#include "esphome/components/display/display.h"
#include "game_of_life.h"

namespace esphome::pixoo64::dashboard {

class GameOfLifeDashboard : public Dashboard {
 public:
  bool available() const override { return true; }
  void OnShow(uint32_t now_ms) override;
  void Tick(uint32_t now_ms) override;
  void Render(display::Display &display) const override;

  void set_seed(uint32_t seed) {
    this->initial_seed_ = seed;
    this->has_fixed_seed_ = true;
  }

 protected:
  void SeedRandomBoard_();
  uint32_t NextRandom_();

  static constexpr uint32_t kGenerationIntervalMs = 150;
  static constexpr uint32_t kNonzeroSeed = 0x6d2b79f5u;
  pixoo::life::GameOfLifeModel model_;
  uint32_t last_step_ms_{0};
  uint32_t initial_seed_{0};
  uint32_t random_state_{kNonzeroSeed};
  bool has_fixed_seed_{false};
};

}  // namespace esphome::pixoo64::dashboard
