#include "game_of_life_dashboard.h"

#include "esphome/core/helpers.h"

namespace esphome::pixoo64::dashboard {

void GameOfLifeDashboard::OnShow(uint32_t now_ms) {
  this->random_state_ =
      this->has_fixed_seed_ ? this->initial_seed_ : random_uint32();
  if (this->random_state_ == 0) this->random_state_ = kNonzeroSeed;
  this->SeedRandomBoard_();
  this->last_step_ms_ = now_ms;
}

void GameOfLifeDashboard::Tick(uint32_t now_ms) {
  if (now_ms - this->last_step_ms_ >= kGenerationIntervalMs) {
    if (!this->model_.Step()) this->SeedRandomBoard_();
    this->last_step_ms_ = now_ms;
  }
}

void GameOfLifeDashboard::Render(display::Display &display) const {
  display.fill(Color(0, 0, 0));
  for (int y = 0; y < pixoo::life::GameOfLifeModel::kHeight; y++) {
    for (int x = 0; x < pixoo::life::GameOfLifeModel::kWidth; x++) {
      if (this->model_.Alive(x, y))
        display.draw_pixel_at(x, y, Color(0, 255, 0));
    }
  }
}

void GameOfLifeDashboard::SeedRandomBoard_() {
  this->model_.Clear();
  for (int y = 0; y < pixoo::life::GameOfLifeModel::kHeight; y++) {
    for (int x = 0; x < pixoo::life::GameOfLifeModel::kWidth; x++) {
      if (this->NextRandom_() % 100u < 30u)
        this->model_.SetAlive(x, y, true);
    }
  }
}

uint32_t GameOfLifeDashboard::NextRandom_() {
  this->random_state_ ^= this->random_state_ << 13;
  this->random_state_ ^= this->random_state_ >> 17;
  this->random_state_ ^= this->random_state_ << 5;
  return this->random_state_;
}

}  // namespace esphome::pixoo64::dashboard
