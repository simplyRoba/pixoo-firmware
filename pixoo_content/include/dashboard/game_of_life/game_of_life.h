#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace pixoo::life {

// A bounded 64x64 Conway board. Coordinates outside the board are dead.
class GameOfLifeModel {
 public:
  static constexpr int kWidth = 64;
  static constexpr int kHeight = 64;
  static constexpr int kCellCount = kWidth * kHeight;

  bool Alive(int x, int y) const;
  void SetAlive(int x, int y, bool alive);
  void Clear();

  // Advances one synchronous generation and reports whether any cell changed.
  bool Step();

 private:
  static bool InBounds(int x, int y);
  static std::size_t Index(int x, int y);

  std::array<uint8_t, kCellCount> current_{};
  std::array<uint8_t, kCellCount> next_{};
};

}  // namespace pixoo::life
