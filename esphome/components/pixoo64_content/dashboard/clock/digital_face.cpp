#include "digital_face.h"

#include "../seven_segment.h"

namespace esphome::pixoo64::clockface {
namespace {

const Color kBackground(0, 2, 6);
const seven_segment::LedStyle kDigitStyle{
    1.45f,
    2.65f,
    0.52f,
    Color(0, 82, 132),
    Color(25, 186, 232),
    Color(184, 248, 255),
    2.0f,
    2.3f,
};

constexpr float kDigitX[pixoo::clock::DigitalClockModel::kDigits] = {
    3.5f, 16.5f, 36.5f, 49.5f};
constexpr float kDigitY = 11.0f;
constexpr float kDigitWidth = 11.0f;
constexpr float kDigitHeight = 40.0f;
constexpr float kColonX = 32.0f;
constexpr float kColonUpperY = 25.0f;
constexpr float kColonLowerY = 39.0f;

}  // namespace

void DigitalFace::Tick(const ClockTime &time, uint32_t now_ms) {
  if (!time.valid) {
    this->model_.Clear();
    return;
  }
  if (this->load_pending_) {
    this->load_pending_ = false;
    this->model_.StartLoad(now_ms);
  }
  this->model_.Update(time.hour, time.minute, time.second, now_ms);
}

void DigitalFace::OnShow(uint32_t now_ms) {
  (void) now_ms;
  this->load_pending_ = true;
}

void DigitalFace::Render(display::Display &display) const {
  display.fill(kBackground);

  for (int digit = 0; digit < pixoo::clock::DigitalClockModel::kDigits;
       digit++) {
    for (int segment = 0;
         segment < pixoo::clock::DigitalClockModel::kSegments; segment++) {
      seven_segment::DrawSegment(
          display, kDigitX[digit], kDigitY, kDigitWidth, kDigitHeight,
          static_cast<uint8_t>(1u << segment), kDigitStyle,
          this->model_.Level(digit, segment));
    }
  }

  const float colon_level = this->model_.ColonLevel();
  seven_segment::DrawLedDot(display, kColonX, kColonUpperY, kDigitStyle,
                            colon_level);
  seven_segment::DrawLedDot(display, kColonX, kColonLowerY, kDigitStyle,
                            colon_level);
}

}  // namespace esphome::pixoo64::clockface
