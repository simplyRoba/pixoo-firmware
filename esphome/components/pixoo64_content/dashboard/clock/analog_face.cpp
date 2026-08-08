#include "analog_face.h"

#include <cassert>
#include <cmath>
#include <memory>
#include <new>

#include "pixoo_cmd.h"

namespace esphome::pixoo64::clockface {
namespace {

using pixoo::clock::HandAngles;

constexpr float kPi = 3.14159265358979323846f;

const Color kBackground(0, 0, 0);
const Color kHourTick(150, 155, 168);
const Color kHalfTick(64, 67, 76);
const Color kHand(235, 238, 245);
const Color kSecond(226, 44, 38);

// Dial center, on the corner between the four middle pixels.
constexpr float kCenter = 32.0f;

// Radii from the dial center; negative is past the center.
constexpr float kTickOuter = 30.0f;
constexpr float kHourTickInner = 23.5f;
constexpr float kHalfTickInner = 27.0f;
constexpr float kHourTickHalfWidth = 0.95f;
constexpr float kHalfTickHalfWidth = 0.55f;

constexpr float kHourTail = -3.5f;
constexpr float kHourTip = 17.0f;
constexpr float kHourBaseHalfWidth = 1.55f;
constexpr float kHourTipHalfWidth = 1.0f;

constexpr float kMinuteTail = -4.5f;
constexpr float kMinuteTip = 26.5f;
constexpr float kMinuteBaseHalfWidth = 1.25f;
constexpr float kMinuteTipHalfWidth = 0.75f;

constexpr float kSecondTail = -7.5f;
constexpr float kSecondTip = 28.0f;
constexpr float kSecondHalfWidth = 0.8f;
constexpr float kSecondWeightAt = -5.4f;
constexpr float kSecondWeightRadius = 1.7f;

constexpr float kCapRadius = 1.6f;
constexpr float kPivotRadius = 0.75f;

// 12 hour ticks, 12 half ticks, two hands, the second hand and its
// counterweight, two pivot caps.
constexpr int kDialShapeCount = 12 + 12;
constexpr int kHandShapeCount = 2 + 2 + 2;
constexpr int kMaxShapes = kDialShapeCount + kHandShapeCount;

// The sweep of a disc whose radius runs from `r0` at (x0, y0) to `r1` at
// (x1, y1), bounded by [bx0, bx1] x [by0, by1]. Every dial element is one.
struct Shape {
  float x0, y0, x1, y1;
  float r0, r1;
  Color color;
  int bx0, by0, bx1, by1;
};

int ClampInt(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

// Fraction of the pixel at (px, py) inside `s`, taking the edge to be one
// pixel wide.
float Coverage(const Shape &s, float px, float py) {
  const float dx = s.x1 - s.x0;
  const float dy = s.y1 - s.y0;
  const float len2 = dx * dx + dy * dy;
  float t = 0.0f;
  if (len2 > 0.0f) {
    t = ((px - s.x0) * dx + (py - s.y0) * dy) / len2;
    t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
  }
  const float ox = px - (s.x0 + t * dx);
  const float oy = py - (s.y0 + t * dy);
  const float d = std::sqrt(ox * ox + oy * oy);
  const float r = s.r0 + t * (s.r1 - s.r0);
  const float c = r - d + 0.5f;
  return c < 0.0f ? 0.0f : (c > 1.0f ? 1.0f : c);
}

Color Over(Color under, Color over, float coverage) {
  const int a = static_cast<int>(coverage * 255.0f + 0.5f);
  const int inv = 255 - a;
  return Color(static_cast<uint8_t>((over.r * a + under.r * inv) / 255),
               static_cast<uint8_t>((over.g * a + under.g * inv) / 255),
               static_cast<uint8_t>((over.b * a + under.b * inv) / 255));
}

template<int Capacity> class ShapeList {
 public:
  // `angle_deg` runs clockwise from 12 o'clock; `from` and `to` are radii
  // along it. Adding more than kMaxShapes shapes is a programming error.
  void AddRadial(float angle_deg, float from, float to, float from_width,
                 float to_width, Color color) {
    const float rad = angle_deg * kPi / 180.0f;
    const float ux = std::sin(rad);
    const float uy = -std::cos(rad);
    this->Add(kCenter + ux * from, kCenter + uy * from, kCenter + ux * to,
              kCenter + uy * to, from_width, to_width, color);
  }

  void AddDot(float angle_deg, float at, float radius, Color color) {
    this->AddRadial(angle_deg, at, at, radius, radius, color);
  }

  void Add(float x0, float y0, float x1, float y1, float r0, float r1,
           Color color) {
    assert(this->count_ < Capacity);
    Shape &s = this->shapes_[this->count_++];
    s = Shape{x0, y0, x1, y1, r0, r1, color, 0, 0, 0, 0};
    const float margin = (r0 > r1 ? r0 : r1) + 1.0f;
    const float lo_x = (x0 < x1 ? x0 : x1) - margin;
    const float hi_x = (x0 > x1 ? x0 : x1) + margin;
    const float lo_y = (y0 < y1 ? y0 : y1) - margin;
    const float hi_y = (y0 > y1 ? y0 : y1) + margin;
    s.bx0 = ClampInt(static_cast<int>(std::floor(lo_x)), 0, pixoo::kWidth - 1);
    s.bx1 = ClampInt(static_cast<int>(std::ceil(hi_x)), 0, pixoo::kWidth - 1);
    s.by0 = ClampInt(static_cast<int>(std::floor(lo_y)), 0, pixoo::kHeight - 1);
    s.by1 = ClampInt(static_cast<int>(std::ceil(hi_y)), 0, pixoo::kHeight - 1);
  }

  int count() const { return this->count_; }
  const Shape &operator[](int i) const { return this->shapes_[i]; }

 private:
  Shape shapes_[Capacity];
  int count_{0};
};

template<int Capacity> void AddTicks(ShapeList<Capacity> &shapes) {
  static_assert(Capacity >= kDialShapeCount);
  for (int i = 0; i < 12; i++) {
    const float hour_angle = i * 30.0f;
    shapes.AddRadial(hour_angle, kHourTickInner, kTickOuter,
                     kHourTickHalfWidth, kHourTickHalfWidth, kHourTick);
    shapes.AddRadial(hour_angle + 15.0f, kHalfTickInner, kTickOuter,
                     kHalfTickHalfWidth, kHalfTickHalfWidth, kHalfTick);
  }
}

template<int Capacity>
void AddHands(ShapeList<Capacity> &shapes, const HandAngles &a) {
  static_assert(Capacity >= kHandShapeCount);
  shapes.AddRadial(a.hour, kHourTail, kHourTip, kHourBaseHalfWidth,
                   kHourTipHalfWidth, kHand);
  shapes.AddRadial(a.minute, kMinuteTail, kMinuteTip, kMinuteBaseHalfWidth,
                   kMinuteTipHalfWidth, kHand);
  shapes.AddDot(0.0f, 0.0f, kCapRadius, kHand);
  shapes.AddDot(a.second, kSecondWeightAt, kSecondWeightRadius, kSecond);
  shapes.AddRadial(a.second, kSecondTail, kSecondTip, kSecondHalfWidth,
                   kSecondHalfWidth, kSecond);
  shapes.AddDot(0.0f, 0.0f, kPivotRadius, kSecond);
}

class Dial {
 public:
  Dial() : pixels_(new (std::nothrow)
                       uint8_t[pixoo::kWidth * pixoo::kHeight * 3]) {
    if (this->pixels_ == nullptr)
      return;

    ShapeList<kDialShapeCount> ticks;
    AddTicks(ticks);
    for (int y = 0; y < pixoo::kHeight; y++) {
      for (int x = 0; x < pixoo::kWidth; x++) {
        const float px = x + 0.5f;
        const float py = y + 0.5f;
        Color pixel = kBackground;
        for (int i = 0; i < ticks.count(); i++) {
          const Shape &s = ticks[i];
          if (x < s.bx0 || x > s.bx1 || y < s.by0 || y > s.by1)
            continue;
          const float coverage = Coverage(s, px, py);
          if (coverage > 0.0f)
            pixel = Over(pixel, s.color, coverage);
        }
        const int index = (y * pixoo::kWidth + x) * 3;
        this->pixels_[index] = pixel.r;
        this->pixels_[index + 1] = pixel.g;
        this->pixels_[index + 2] = pixel.b;
      }
    }
  }

  bool valid() const { return this->pixels_ != nullptr; }

  Color At(int x, int y) const {
    const int index = (y * pixoo::kWidth + x) * 3;
    return Color(this->pixels_[index], this->pixels_[index + 1],
                 this->pixels_[index + 2]);
  }

 private:
  std::unique_ptr<uint8_t[]> pixels_;
};

const Dial &StaticDial() {
  static const Dial dial;
  return dial;
}

template<int Capacity>
void Raster(display::Display &display, const ShapeList<Capacity> &shapes,
            const Dial *dial) {
  for (int y = 0; y < pixoo::kHeight; y++) {
    for (int x = 0; x < pixoo::kWidth; x++) {
      const float px = x + 0.5f;
      const float py = y + 0.5f;
      Color pixel = dial == nullptr ? kBackground : dial->At(x, y);
      for (int i = 0; i < shapes.count(); i++) {
        const Shape &s = shapes[i];
        if (x < s.bx0 || x > s.bx1 || y < s.by0 || y > s.by1)
          continue;
        const float coverage = Coverage(s, px, py);
        if (coverage > 0.0f)
          pixel = Over(pixel, s.color, coverage);
      }
      display.draw_pixel_at(x, y, pixel);
    }
  }
}

void RenderUncached(display::Display &display, const HandAngles &a) {
  ShapeList<kMaxShapes> shapes;
  AddTicks(shapes);
  AddHands(shapes, a);
  Raster(display, shapes, nullptr);
}

}  // namespace

void AnalogFace::Tick(const ClockTime &time, uint32_t now_ms) {
  if (!time.valid) {
    // The hands wait at 12, where the wind starts once the time arrives.
    this->model_.Park();
    return;
  }
  if (this->wind_pending_) {
    // The wind runs onto the current time, which is only known once the clock
    // is valid.
    this->wind_pending_ = false;
    this->model_.StartWind(now_ms);
  }
  this->model_.Update(time.hour, time.minute, time.second, now_ms);
}

void AnalogFace::OnShow(uint32_t now_ms) {
  (void) now_ms;
  this->wind_pending_ = true;
}

void AnalogFace::Render(display::Display &display) const {
  const Dial &dial = StaticDial();
  const HandAngles a = this->model_.Angles();
  if (!dial.valid()) {
    RenderUncached(display, a);
    return;
  }

  ShapeList<kHandShapeCount> hands;
  AddHands(hands, a);
  Raster(display, hands, &dial);
}

}  // namespace esphome::pixoo64::clockface
