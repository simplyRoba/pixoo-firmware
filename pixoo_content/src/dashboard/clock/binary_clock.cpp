#include "binary_clock.h"

namespace pixoo::clock {
namespace {

int Wrap(int value, int modulus) {
  const int r = value % modulus;
  return r < 0 ? r + modulus : r;
}

// Unsigned subtraction wraps, so this holds across the 32-bit millis rollover.
uint32_t Elapsed(uint32_t now_ms, uint32_t since_ms) { return now_ms - since_ms; }

// True while `deadline_ms` is still ahead of `now_ms`, tolerating the rollover
// for deadlines within half the counter's range.
bool Pending(uint32_t now_ms, uint32_t deadline_ms) {
  return static_cast<int32_t>(now_ms - deadline_ms) < 0;
}

float Smoothstep(float t) {
  if (t <= 0.0f)
    return 0.0f;
  if (t >= 1.0f)
    return 1.0f;
  return t * t * (3.0f - 2.0f * t);
}

// Level of a dot lighting up, `t` running 0..1 across the pop: it drives past
// its resting level, then eases back onto it.
float PopProfile(float t) {
  constexpr float kPeak = BinaryClockModel::kPopPeak;
  constexpr float kOver = BinaryClockModel::kPopOvershoot;
  if (t <= kPeak)
    return (1.0f + kOver) * Smoothstep(t / kPeak);
  return 1.0f + kOver * (1.0f - Smoothstep((t - kPeak) / (1.0f - kPeak)));
}

}  // namespace

int BinaryClockModel::RowBit(int row) {
  switch (row) {
    case 0:
      return 8;
    case 1:
      return 4;
    case 2:
      return 2;
    case 3:
      return 1;
    default:
      return 0;
  }
}

void BinaryClockModel::Digits(int hour, int minute, int second,
                              int digits[kColumns]) {
  const int h = Wrap(hour, 24);
  const int m = Wrap(minute, 60);
  const int s = Wrap(second, 60);
  digits[0] = h / 10;
  digits[1] = h % 10;
  digits[2] = m / 10;
  digits[3] = m % 10;
  digits[4] = s / 10;
  digits[5] = s % 10;
}

uint32_t BinaryClockModel::LoadMs() {
  return (kColumns - 1) * kLoadStaggerMs + kPopMs;
}

void BinaryClockModel::StartLoad(uint32_t now_ms) {
  this->Clear();
  this->load_started_ms_ = now_ms;
  this->loading_ = true;
}

void BinaryClockModel::Clear() {
  for (auto &column : this->dots_) {
    for (Dot &dot : column)
      dot = Dot{};
  }
  this->loading_ = false;
}

void BinaryClockModel::Advance_(Dot &dot, uint32_t now_ms) {
  if (!dot.animating)
    return;
  if (Pending(now_ms, dot.start_ms))
    return;
  const uint32_t span = dot.lit ? kPopMs : kGlowMs;
  const uint32_t elapsed = Elapsed(now_ms, dot.start_ms);
  if (elapsed >= span) {
    dot.level = dot.lit ? 1.0f : 0.0f;
    dot.animating = false;
    return;
  }
  const float t = static_cast<float>(elapsed) / static_cast<float>(span);
  if (dot.lit)
    dot.level = dot.from + (1.0f - dot.from) * PopProfile(t);
  else
    dot.level = dot.from * (1.0f - Smoothstep(t));
}

void BinaryClockModel::Retarget_(int column, int row, bool lit,
                                 uint32_t start_ms) {
  Dot &dot = this->dots_[column][row];
  if (dot.lit == lit)
    return;
  dot.lit = lit;
  dot.from = dot.level;
  dot.start_ms = start_ms;
  dot.animating = true;
}

void BinaryClockModel::Update(int hour, int minute, int second,
                              uint32_t now_ms) {
  for (auto &column : this->dots_) {
    for (Dot &dot : column)
      Advance_(dot, now_ms);
  }

  int digits[kColumns];
  Digits(hour, minute, second, digits);

  if (this->loading_) {
    // Every lit dot arrives with the sweep over its own column.
    this->loading_ = false;
    for (int c = 0; c < kColumns; c++) {
      const uint32_t start = this->load_started_ms_ + c * kLoadStaggerMs;
      for (int r = 0; r < kRows; r++)
        this->Retarget_(c, r, (digits[c] & RowBit(r)) != 0, start);
    }
  } else {
    // A carry runs into the higher digits, so the change starts in the
    // rightmost column that moved and reaches each column left of it a beat
    // later.
    int last_changed = -1;
    for (int c = 0; c < kColumns; c++) {
      for (int r = 0; r < kRows; r++) {
        if (this->dots_[c][r].lit != ((digits[c] & RowBit(r)) != 0))
          last_changed = c;
      }
    }
    for (int c = 0; c <= last_changed; c++) {
      const uint32_t start = now_ms + (last_changed - c) * kRippleMs;
      for (int r = 0; r < kRows; r++)
        this->Retarget_(c, r, (digits[c] & RowBit(r)) != 0, start);
    }
  }

  for (auto &column : this->dots_) {
    for (Dot &dot : column)
      Advance_(dot, now_ms);
  }
}

float BinaryClockModel::Level(int column, int row) const {
  if (column < 0 || column >= kColumns || row < 0 || row >= kRows)
    return 0.0f;
  return this->dots_[column][row].level;
}

}  // namespace pixoo::clock
