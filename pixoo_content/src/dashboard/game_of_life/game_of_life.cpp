#include "game_of_life.h"

namespace pixoo::life {

bool GameOfLifeModel::Alive(int x, int y) const {
  return InBounds(x, y) && this->current_[Index(x, y)] != 0;
}

void GameOfLifeModel::SetAlive(int x, int y, bool alive) {
  if (InBounds(x, y))
    this->current_[Index(x, y)] = alive ? 1 : 0;
}

void GameOfLifeModel::Clear() {
  this->current_.fill(0);
  this->next_.fill(0);
}

bool GameOfLifeModel::Step() {
  bool changed = false;
  for (int y = 0; y < kHeight; y++) {
    for (int x = 0; x < kWidth; x++) {
      int alive_neighbours = 0;
      for (int dy = -1; dy <= 1; dy++) {
        for (int dx = -1; dx <= 1; dx++) {
          if (dy == 0 && dx == 0) continue;
          if (Alive(x + dx, y + dy)) {
            alive_neighbours++;
          }
        }
      }

      const bool alive = Alive(x, y);
      bool alive_in_next_generation;
      if (alive) {
        alive_in_next_generation = alive_neighbours == 2 || alive_neighbours == 3;
      } else {
        alive_in_next_generation = alive_neighbours == 3;
      }
      next_[Index(x, y)] = alive_in_next_generation ? 1 : 0;
      changed = changed || alive != alive_in_next_generation;
    }
  }

  this->current_.swap(this->next_);
  return changed;
}

bool GameOfLifeModel::InBounds(int x, int y) {
  return x >= 0 && x < kWidth && y >= 0 && y < kHeight;
}

std::size_t GameOfLifeModel::Index(int x, int y) {
  return static_cast<std::size_t>(y * kWidth + x);
}

}  // namespace pixoo::life
