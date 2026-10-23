#include "shadow_face.h"

#include <array>
#include <cmath>
#include <limits>
#include <new>

#include "esphome/components/pixoo64_content/blend_canvas.h"
#include "pixoo_cmd.h"

#if defined(USE_ESP32)
#include "sdkconfig.h"
#include "esphome/core/log.h"
#if !defined(CONFIG_FREERTOS_UNICORE) || !CONFIG_FREERTOS_UNICORE
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#define SHADOW_PARALLEL
#endif
#endif

// Balance shader execution speed with its code footprint.
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC push_options
#pragma GCC optimize("O2")
#endif

namespace esphome::pixoo64::clockface {
namespace {

constexpr float kRadians = 3.14159265358979323846f / 180.0f;
constexpr int kOne = 256;
constexpr int kDirectionScale = 32768;
constexpr int kHourLength = 16 * kOne;
constexpr int kMinuteLength = 27 * kOne;
constexpr int kHourWidth = kOne / 2;
constexpr int kMinuteWidth = kOne / 2;
constexpr int kSecondLength = 28 * kOne;
constexpr int kSecondWidth = 77;
constexpr int kSecondTail = 0;
constexpr int kSecondOcclusion = 128;
constexpr int kSecondStrokeWidth = kOne / 2;
constexpr int kSecondInk = detail::kShadowSecondInk;
constexpr int kMarkerCount = 4;
constexpr int kMarkerDistance = 29 * kOne;
constexpr int kMarkerRadius = kOne;
constexpr int kMarkerBand = 4;
constexpr int kMarkerResponse = 54;
static_assert(kMarkerDistance - kMarkerRadius >= kMinuteLength + kOne / 2 + kMinuteWidth);
static_assert(kMarkerDistance - kMarkerRadius - kOne / 2 >=
              (pixoo::kWidth - 1 - 2 * kMarkerBand) * kOne / 2);

constexpr int ClampByte(int value) {
  return value < 0 ? 0 : (value > 255 ? 255 : value);
}
constexpr int Abs(int value) { return value < 0 ? -value : value; }
constexpr int Max(int a, int b) { return a > b ? a : b; }
constexpr int Min(int a, int b) { return a < b ? a : b; }
// Rounded multiplication of two byte-valued light or coverage levels.
constexpr int Mul(int a, int b) {
  const int product = a * b + 128;
  return (product + (product >> 8)) >> 8;
}
constexpr int Screen(int a, int b) { return a + Mul(255 - a, b); }

constexpr std::array<uint8_t, 256> MakeEmission() {
  std::array<uint8_t, 256> result{};
  for (int i = 0; i < 256; ++i)
    result[i] = static_cast<uint8_t>(((255 - i) * (255 - i) + 127) / 255);
  return result;
}
constexpr auto kEmission = MakeEmission();
constexpr std::array<uint8_t, 256> MakeScaledEmission(int scale) {
  std::array<uint8_t, 256> result{};
  for (int i = 0; i < 256; ++i)
    result[i] = static_cast<uint8_t>(Mul(kEmission[i], scale));
  return result;
}
constexpr auto kEmissionNear = MakeScaledEmission(130);
constexpr auto kEmissionDiffuse = MakeScaledEmission(120);
constexpr auto kEmissionCrease = MakeScaledEmission(245);

constexpr std::array<uint32_t, 512> MakeReciprocal() {
  std::array<uint32_t, 512> result{};
  for (uint32_t i = 1; i < result.size(); ++i)
    result[i] = 16711680u / i;
  return result;
}
constexpr auto kReciprocal = MakeReciprocal();

int EmissionFacing(int along, int across, bool cap) {
  const int front = Max(cap ? along : across, 0);
  if (front == 0)
    return along == 0 && across == 0 ? 255 : 0;
  const int a = Abs(along), b = Abs(across);
  const int distance = Max(a, b) + Min(a, b) * 3 / 8;
  if (distance == 0)
    return 255;
  // Interpolate the reciprocal profile so subpixel motion cannot jump a bin.
  const int scaled = Min(Max(distance, 64), 511 * 64);
  const int index = scaled / 64;
  const int fraction = scaled % 64;
  const uint32_t reciprocal = kReciprocal[index] -
      (kReciprocal[index] - kReciprocal[Min(index + 1, 511)]) * fraction / 64;
  return ClampByte(static_cast<int>((static_cast<uint32_t>(front) *
                                     reciprocal) >> 22));
}

struct Point { int x, y; };
constexpr std::array<Point, kMarkerCount> kMarkerCenters = {{
    {0, -kMarkerDistance}, {kMarkerDistance, 0},
    {0, kMarkerDistance}, {-kMarkerDistance, 0}}};
Point Sub(Point a, Point b) { return {a.x - b.x, a.y - b.y}; }
int Cross(Point a, Point b) { return a.x * b.y - a.y * b.x; }

// Q15 directions retain subpixel motion. Screen projections remain Q16 until
// each pixel is sampled; geometry and coverage use Q8 local pixels.
struct Direction {
  int x, y;
  explicit Direction(float degrees)
      : x(static_cast<int>(std::lround(std::sin(degrees * kRadians) * kDirectionScale))),
        y(static_cast<int>(std::lround(-std::cos(degrees * kRadians) * kDirectionScale))) {}

  Point World(int along, int across) const {
    return {(along * this->x - across * this->y) / kDirectionScale,
            (along * this->y + across * this->x) / kDirectionScale};
  }
  Point Local(Point p) const {
    return {(p.x * this->x + p.y * this->y) / kDirectionScale,
            (-p.x * this->y + p.y * this->x) / kDirectionScale};
  }
};

// Rounded, finite hand body. The octagonal norm only affects subpixel corners.
int BodyDistance(int along, int across, int length, int width, int tail = 0) {
  const int radius = Min(width, 154);
  const int qa = Abs(along - (length - tail) / 2) -
                 (length + tail) / 2 + radius;
  const int qs = Abs(across) - width + radius;
  const int oa = Max(qa, 0), os = Max(qs, 0);
  return Max(oa, os) + Min(oa, os) * 3 / 8 + Min(Max(qa, qs), 0) - radius;
}

int BodyCoverage(int along, int across, int length, int width, int tail = 0) {
  if (along <= -tail - 128 || along >= length + 128 || Abs(across) >= width + 128)
    return 0;
  return ClampByte(128 - BodyDistance(along, across, length, width, tail));
}

std::array<Point, 4> BodyCorners(const Direction &direction, int length, int width,
                                int tail = 0) {
  return {direction.World(-tail, -width), direction.World(length, -width),
          direction.World(length, width), direction.World(-tail, width)};
}

struct Interval {
  int lo, hi;

  bool Clip(int p, int c) {
    const int left = p * this->lo + c;
    const int right = p * this->hi + c;
    if (left >= 0 && right >= 0)
      return true;
    if (left < 0 && right < 0)
      return false;
    if (p > 0)
      this->lo = (-c + p - 1) / p;
    else
      this->hi = c / -p;
    return this->lo < this->hi;
  }

  bool Ray(Point ray, Point q, int offset, int sign) {
    return this->Clip(-ray.y * sign,
        (ray.x * (offset - q.y) + ray.y * q.x) * sign);
  }

};

struct FaceEdge {
  int dx, dy, c;
  Interval source;
  bool valid;
};

struct FrontSpan { int start, last; };
constexpr std::array<FrontSpan, 16> MakeFrontSpans() {
  std::array<FrontSpan, 16> spans{};
  for (int mask = 0; mask < 16; ++mask) {
    int start = -1;
    for (int i = 0; i < 4; ++i) {
      if ((mask & (1 << i)) && !(mask & (1 << ((i + 3) % 4)))) {
        start = i;
        break;
      }
    }
    const int next = (start + 1) % 4;
    spans[mask] = {start, start >= 0 && (mask & (1 << next)) ? next : start};
  }
  return spans;
}
constexpr auto kFrontSpans = MakeFrontSpans();

struct EdgeBlocker {
  std::array<Point, 4> corners;
  int min_a, max_a, min_s, max_s;
  std::array<FaceEdge, 4> faces;
  int receiver_min_a, receiver_max_a, receiver_min_s, receiver_max_s;

  void Prepare(int length, int offset) {
    this->min_a = this->max_a = this->corners[0].x;
    this->min_s = this->max_s = this->corners[0].y;
    for (int i = 1; i < 4; ++i) {
      this->min_a = Min(this->min_a, this->corners[i].x);
      this->max_a = Max(this->max_a, this->corners[i].x);
      this->min_s = Min(this->min_s, this->corners[i].y);
      this->max_s = Max(this->max_s, this->corners[i].y);
    }
    this->receiver_min_a = this->min_a > length ? this->min_a : std::numeric_limits<int>::min();
    this->receiver_max_a = this->max_a < 0 ? this->max_a : std::numeric_limits<int>::max();
    this->receiver_min_s = this->min_s > offset ? this->min_s : std::numeric_limits<int>::min();
    this->receiver_max_s = this->max_s < offset ? this->max_s : std::numeric_limits<int>::max();
    for (int i = 0; i < 4; ++i) {
      const Point a = this->corners[i];
      const Point d = Sub(this->corners[(i + 1) % 4], a);
      auto &face = this->faces[i];
      face.dx = d.x;
      face.dy = d.y;
      face.c = d.y * a.x - d.x * a.y;
      face.source = {0, length};
      face.valid = face.source.Clip(-d.y, d.x * offset + face.c);
    }
  }

  bool CouldBlock(Point q) const {
    return q.x >= this->receiver_min_a && q.x <= this->receiver_max_a &&
           q.y >= this->receiver_min_s && q.y <= this->receiver_max_s;
  }

  bool ClipFace(int index, Interval &interval) const {
    const auto &face = this->faces[index];
    if (!face.valid)
      return false;
    interval.lo = Max(interval.lo, face.source.lo);
    interval.hi = Min(interval.hi, face.source.hi);
    return interval.lo < interval.hi;
  }
};

struct LineSource {
  // The emitting body cannot block rays to front-facing receivers.
  // Other hand, seconds hand, then four opaque cardinal markers.
  std::array<EdgeBlocker, 2 + kMarkerCount> bodies;
  int length, edge_offset;
};

struct Shadows {
  LineSource edge, cap;

  Shadows(const Direction &emitter, int length, int width, int side,
          const Direction &blocker, int blocker_length, int blocker_width,
          const Direction &second)
      : edge{{}, length, width + 16}, cap{{}, 2 * width, length + 128} {
    const auto other_body = BodyCorners(blocker, blocker_length, blocker_width);
    const auto second_body = BodyCorners(second, kSecondLength, kSecondWidth, kSecondTail);
    for (int i = 0; i < 4; ++i) {
      // Mirroring across the emitter reverses winding; retain CCW bodies.
      const int index = side > 0 ? i : 3 - i;
      Point other = emitter.Local(other_body[index]);
      Point seconds = emitter.Local(second_body[index]);
      this->edge.bodies[0].corners[i] = {other.x, side * other.y};
      this->edge.bodies[1].corners[i] = {seconds.x, side * seconds.y};
    }
    for (int marker = 0; marker < kMarkerCount; ++marker) {
      const Point c = kMarkerCenters[marker];
      const std::array<Point, 4> corners = {{{c.x - kMarkerRadius, c.y - kMarkerRadius},
          {c.x + kMarkerRadius, c.y - kMarkerRadius},
          {c.x + kMarkerRadius, c.y + kMarkerRadius},
          {c.x - kMarkerRadius, c.y + kMarkerRadius}}};
      for (int i = 0; i < 4; ++i) {
        const Point p = emitter.Local(corners[side > 0 ? i : 3 - i]);
        this->edge.bodies[marker + 2].corners[i] = {p.x, side * p.y};
      }
    }
    for (int body = 0; body < 2 + kMarkerCount; ++body) {
      // Swapping axes reverses winding. The tip spans the full hand width.
      for (int i = 0; i < 4; ++i) {
        const Point p = this->edge.bodies[body].corners[3 - i];
        this->cap.bodies[body].corners[i] = {p.y + width, p.x};
      }
      this->edge.bodies[body].Prepare(this->edge.length, this->edge.edge_offset);
      this->cap.bodies[body].Prepare(this->cap.length, this->cap.edge_offset);
    }
  }
};


bool BlockedInterval(const EdgeBlocker &blocker, Point q,
                     int length, int offset, Interval &interval) {
  // The receiver/source hull is a triangle; its bounds reject distant bodies.
  if (!blocker.CouldBlock(q))
    return false;
  const auto &body = blocker.corners;
  int front_mask = 0;
  for (int i = 0; i < 4; ++i) {
    const auto &face = blocker.faces[i];
    if (-face.dy * q.x + face.dx * q.y + face.c < 0)
      front_mask |= 1 << i;
  }
  const auto span = kFrontSpans[front_mask];
  const int start = span.start, last = span.last;
  interval = {0, length};
  if (start < 0)
    return true;  // Receiver inside the blocking body.
  if (!blocker.ClipFace(start, interval) ||
      (last != start && !blocker.ClipFace(last, interval)))
    return false;
  const Point a = Sub(body[start], q);
  const Point b = Sub(body[(last + 1) % 4], q);
  const int sign = Cross(a, b) >= 0 ? 1 : -1;
  return interval.Ray(a, q, offset, sign) &&
         interval.Ray(b, q, offset, -sign);
}

// atan(delta / normal), with a quarter turn represented by 32768.
int EdgeIntegral(int delta, int normal) {
  const int magnitude = Abs(delta);
  const int r = Min(magnitude, normal) * 4096 / Max(magnitude, normal);
  int angle = (16384 + 5695 * (4096 - r) / 4096) * r / 4096;
  if (magnitude > normal)
    angle = 32768 - angle;
  return delta < 0 ? -angle : angle;
}

int LineVisibility(const LineSource &shadows, int along, int across,
                   int second_occlusion = kSecondOcclusion) {
  Interval other, seconds;
  const bool has_other = BlockedInterval(shadows.bodies[0], {along, across},
      shadows.length, shadows.edge_offset, other);
  if (has_other && other.lo == 0 && other.hi == shadows.length)
    return 0;
  const bool has_seconds = second_occlusion != 0 &&
      BlockedInterval(shadows.bodies[1], {along, across},
                      shadows.length, shadows.edge_offset, seconds);
  if (!has_other && !has_seconds)
    return 255;
  if (!has_other && seconds.lo == 0 && seconds.hi == shadows.length)
    return 255 - second_occlusion;
  const int normal = Max(Abs(across - shadows.edge_offset), kOne);
  const int end_integral = EdgeIntegral(shadows.length - along, normal);
  const int start_integral = EdgeIntegral(-along, normal);
  const int full = end_integral - start_integral;
  if (full <= 0)
    return 255;
  const auto integral = [&](int point) {
    return point == 0 ? start_integral : point == shadows.length ? end_integral
        : EdgeIntegral(point - along, normal);
  };
  int other_lo = 0, other_hi = 0, other_covered = 0;
  if (has_other) {
    other_lo = integral(other.lo);
    other_hi = integral(other.hi);
    other_covered = other_hi - other_lo;
  }
  int seconds_covered = 0;
  if (has_seconds) {
    if (!has_other || seconds.hi <= other.lo || seconds.lo >= other.hi) {
      seconds_covered = integral(seconds.hi) - integral(seconds.lo);
    } else {
      // Keep the overlap with the opaque hand fully blocked.
      if (seconds.lo < other.lo)
        seconds_covered += other_lo - integral(seconds.lo);
      if (seconds.hi > other.hi)
        seconds_covered += integral(seconds.hi) - other_hi;
    }
  }
  return ClampByte((255 * (full - other_covered) -
                    second_occlusion * seconds_covered) / full);
}

int MarkerVisibility(const LineSource &shadows, int along, int across,
                     int marker_mask, int unmarked,
                     int second_occlusion = kSecondOcclusion) {
  if (unmarked == 0 || marker_mask == 0)
    return unmarked;
  std::array<Interval, 1 + kMarkerCount> opaque;
  int count = 0;
  const auto add = [&](Interval interval) {
    int at = count;
    while (at > 0 && opaque[at - 1].lo > interval.lo) {
      opaque[at] = opaque[at - 1];
      --at;
    }
    opaque[at] = interval;
    ++count;
  };
  Interval interval;
  bool marker_blocked = false;
  for (int marker = 0; marker < kMarkerCount; ++marker) {
    if ((marker_mask & (1 << marker)) == 0)
      continue;
    if (BlockedInterval(shadows.bodies[marker + 2], {along, across}, shadows.length,
                        shadows.edge_offset, interval)) {
      if (interval.lo == 0 && interval.hi == shadows.length)
        return 0;
      add(interval);
      marker_blocked = true;
    }
  }
  if (!marker_blocked)
    return unmarked;
  if (BlockedInterval(shadows.bodies[0], {along, across}, shadows.length,
                      shadows.edge_offset, interval))
    add(interval);
  int merged = 0;
  for (int i = 0; i < count; ++i) {
    if (merged != 0 && opaque[i].lo <= opaque[merged - 1].hi)
      opaque[merged - 1].hi = Max(opaque[merged - 1].hi, opaque[i].hi);
    else
      opaque[merged++] = opaque[i];
  }
  if (merged == 1 && opaque[0].lo == 0 && opaque[0].hi == shadows.length)
    return 0;
  const int normal = Max(Abs(across - shadows.edge_offset), kOne);
  const int end_integral = EdgeIntegral(shadows.length - along, normal);
  const int start_integral = EdgeIntegral(-along, normal);
  const int full = end_integral - start_integral;
  if (full <= 0)
    return unmarked;
  const auto integral = [&](int point) {
    return point == 0 ? start_integral : point == shadows.length ? end_integral
        : EdgeIntegral(point - along, normal);
  };
  Interval seconds;
  const bool has_seconds = second_occlusion != 0 &&
      BlockedInterval(shadows.bodies[1], {along, across},
                      shadows.length, shadows.edge_offset, seconds);
  int seconds_covered = has_seconds ? integral(seconds.hi) - integral(seconds.lo) : 0;
  int opaque_covered = 0;
  for (int i = 0; i < merged; ++i) {
    opaque_covered += integral(opaque[i].hi) - integral(opaque[i].lo);
    if (has_seconds) {
      const int lo = Max(opaque[i].lo, seconds.lo);
      const int hi = Min(opaque[i].hi, seconds.hi);
      if (lo < hi)
        seconds_covered -= integral(hi) - integral(lo);
    }
  }
  return ClampByte((255 * (full - opaque_covered) -
                    second_occlusion * seconds_covered) / full);
}

// Marker bounds align with pixel edges; coverage matches the shadow quad.
int MarkerCoverage(int marker, int x, int y) {
  const Point c = kMarkerCenters[marker];
  const int dx = Abs(x - c.x), dy = Abs(y - c.y);
  return Mul(ClampByte(kMarkerRadius + kOne / 2 - dx),
             ClampByte(kMarkerRadius + kOne / 2 - dy));
}

struct LightSample {
  int edge_light, cap_light, body, crease, cap_crease;
  Point edge_surface;
};

LightSample SampleHand(int along, int across, int length, int width) {
  const int body = BodyCoverage(along, across, length, width);
  // Continuous edge and tip profiles form the unoccluded light field.
  // Each component has its own source visibility.
  const int end = Max(Max(-along, along - length), 0);
  const int side = across - width - 128;
  const int edge_front = across - width - 16;
  const int cap_along = along - length - 128;
  const bool edge_floor = edge_front > 0 || (edge_front == 0 && end == 0);
  const bool cap_floor = cap_along > 0 || (cap_along == 0 && across == 0);
  // Outside these bounds the crease table index saturates to its zero entry.
  const bool edge_crease = Abs(across) <= 361 && end <= 361;
  const bool tip_crease = Abs(across) <= 361 && Abs(cap_along) <= 361;
  int edge_light = 0, cap_light = 0, crease = 0, cap_crease = 0;
  if (edge_floor || edge_crease) {
    const int squared = side * side + end * end;
    if (edge_floor) {
      const int field = Screen(kEmissionNear[Min(squared >> 15, 255)],
                               kEmissionDiffuse[Min(squared >> 19, 255)]);
      edge_light = Mul(field, EmissionFacing(end, edge_front, false));
    }
    // Direct emission follows the hand axis, not an offset beside its body.
    if (edge_crease)
      crease = Mul(kEmissionCrease[Min((across * across + end * end) >> 9, 255)],
                   ClampByte(along + kOne / 2));
  }
  if (cap_floor || tip_crease) {
    const int squared = across * across + cap_along * cap_along;
    if (cap_floor) {
      const int field = Screen(kEmissionNear[Min(squared >> 15, 255)],
                               kEmissionDiffuse[Min(squared >> 19, 255)]);
      cap_light = Mul(field, EmissionFacing(cap_along, across, true));
    }
    if (tip_crease)
      cap_crease = kEmissionCrease[Min(squared >> 9, 255)];
  }
  return {edge_light, cap_light, body, crease, cap_crease,
          {Min(Max(along, 0), length), width}};
}

// Direct emission is clipped at its physical source before raster coverage.
int SurfaceGain(int emission, Point surface, const Direction &emitter, int side,
                const Direction &other, int other_length, int other_width,
                const Direction &second, bool clip_other = true,
                int second_occlusion = kSecondOcclusion) {
  if (emission == 0)
    return 255;
  const Point source = emitter.World(surface.x, side * surface.y);
  int other_gain = 255;
  if (clip_other) {
    const Point other_local = other.Local(source);
    other_gain = ClampByte(128 + BodyDistance(
        other_local.x, other_local.y, other_length, other_width));
  }
  const Point second_local = second.Local(source);
  const int second_gain = ClampByte(128 + BodyDistance(
      second_local.x, second_local.y, kSecondLength, kSecondWidth, kSecondTail));
  return Mul(other_gain, 255 - Mul(second_occlusion, 255 - second_gain));
}

int Shade(const LightSample &hour, const LightSample &minute,
          int hour_edge_visible, int hour_cap_visible,
          int minute_edge_visible, int minute_cap_visible,
          int second_body, int second) {
  if ((hour.edge_light | hour.cap_light | minute.edge_light | minute.cap_light |
       hour.crease | minute.crease) == 0)
    return Mul(kSecondInk, second);
  const int outside_hands = 255 - Max(hour.body, minute.body);
  const int hour_light = Screen(Mul(hour.edge_light, hour_edge_visible),
                                Mul(hour.cap_light, hour_cap_visible));
  const int minute_light = Screen(Mul(minute.edge_light, minute_edge_visible),
                                  Mul(minute.cap_light, minute_cap_visible));
  int value = Mul(Screen(hour_light, minute_light), outside_hands);
  const int hour_edge = Mul(hour.crease, 255 - minute.body);
  const int minute_edge = minute.crease;
  value = Screen(value, Screen(hour_edge, minute_edge));
  value = Mul(value, 255 - second_body);
  return Mul(value, 255 - second) + Mul(kSecondInk, second);
}

}  // namespace

uint8_t detail::ShadowSecondCoverage(int along, int across) {
  return BodyCoverage(along, across, kSecondLength, kSecondStrokeWidth, kSecondTail);
}

void ShadowFace::OnShow(uint32_t now_ms) {
  (void) now_ms;
  this->load_pending_ = true;
}

void ShadowFace::Tick(const ClockTime &time, uint32_t now_ms) {
  if (!time.valid) {
    this->model_.Clear();
    this->load_pending_ = true;
    return;
  }
  if (this->load_pending_) {
    this->load_pending_ = false;
    this->model_.StartLoad(now_ms);
  }
  this->model_.Update(time.hour, time.minute, time.second, now_ms);
}

namespace {

struct SceneSnapshot {
  Direction hour, minute, second;
  Shadows hour_shadows, minute_shadows;
  int hour_level, minute_level, second_level, second_occlusion;

  SceneSnapshot(const pixoo::clock::ShadowHandAngles &angles,
                const pixoo::clock::ShadowHandLevels &levels)
      : hour(angles.hour), minute(angles.minute), second(angles.second),
        hour_shadows(hour, kHourLength, kHourWidth, 1,
                     minute, kMinuteLength, kMinuteWidth, second),
        minute_shadows(minute, kMinuteLength, kMinuteWidth, -1,
                       hour, kHourLength, kHourWidth, second),
        hour_level(ClampByte(static_cast<int>(levels.hour * 255.0f + .5f))),
        minute_level(ClampByte(static_cast<int>(levels.minute * 255.0f + .5f))),
        second_level(ClampByte(static_cast<int>(levels.second * 255.0f + .5f))),
        second_occlusion(Mul(kSecondOcclusion, second_level)) {}
};

static_assert(sizeof(SceneSnapshot) <= 4096);

// Output starts at first_row and retains screen-row spacing. Signed division
// retains C++ truncation toward zero for negative subpixel projections.
void RenderRows(const SceneSnapshot &scene, int first_row, int last_row,
                uint8_t *gray, int row_step = 1) {
  const auto &hour = scene.hour, &minute = scene.minute, &second = scene.second;
  const auto &hour_shadows = scene.hour_shadows;
  const auto &minute_shadows = scene.minute_shadows;
  const int hour_level = scene.hour_level, minute_level = scene.minute_level;
  const int second_level = scene.second_level, second_occlusion = scene.second_occlusion;
  for (int y = first_row; y < last_row; y += row_step) {
    uint8_t *row = gray + (y - first_row) * pixoo::kWidth;
    const int dy = 2 * y - 63;
    int ah = -63 * hour.x + dy * hour.y;
    int sh = 63 * hour.y + dy * hour.x;
    int am = -63 * minute.x + dy * minute.y;
    int sm = 63 * minute.y + dy * minute.x;
    int as = -63 * second.x + dy * second.y;
    int ss = 63 * second.y + dy * second.x;
    for (int x = 0; x < pixoo::kWidth; ++x) {
      const int h_along = ah / 256, h_across = sh / 256;
      const int m_along = am / 256, m_across = -sm / 256;
      LightSample h = SampleHand(h_along, h_across, kHourLength, kHourWidth);
      LightSample m = SampleHand(m_along, m_across, kMinuteLength, kMinuteWidth);
      if (h.crease != 0 || h.cap_crease != 0) {
        h.crease = Max(Mul(h.crease, SurfaceGain(h.crease, h.edge_surface, hour, 1,
            minute, kMinuteLength, kMinuteWidth, second, true, second_occlusion)),
            Mul(h.cap_crease, SurfaceGain(h.cap_crease, {kHourLength, 0}, hour, 1,
            minute, kMinuteLength, kMinuteWidth, second, true, second_occlusion)));
      }
      if (m.crease != 0 || m.cap_crease != 0) {
        // The minute surface is above the hour surface.
        m.crease = Max(Mul(m.crease, SurfaceGain(m.crease, m.edge_surface, minute, -1,
            hour, kHourLength, kHourWidth, second, false, second_occlusion)),
            Mul(m.cap_crease, SurfaceGain(m.cap_crease, {kMinuteLength, 0}, minute, -1,
            hour, kHourLength, kHourWidth, second, false, second_occlusion)));
      }
      if (hour_level != 255) {
        h.edge_light = Mul(h.edge_light, hour_level);
        h.cap_light = Mul(h.cap_light, hour_level);
        h.crease = Mul(h.crease, hour_level);
      }
      if (minute_level != 255) {
        m.edge_light = Mul(m.edge_light, minute_level);
        m.cap_light = Mul(m.cap_light, minute_level);
        m.crease = Mul(m.crease, minute_level);
      }
      const int s_along = as / 256, s_across = ss / 256;
      const int second_body = Mul(second_occlusion, BodyCoverage(
          s_along, s_across, kSecondLength, kSecondWidth, kSecondTail));
      const bool exposed = Max(Max(h.body, m.body), second_body) < 255;
      const int he = exposed && h.edge_light != 0
          ? LineVisibility(hour_shadows.edge, h_along, h_across, second_occlusion) : 0;
      const int hc = exposed && h.cap_light != 0
          ? LineVisibility(hour_shadows.cap, h_across + kHourWidth, h_along, second_occlusion) : 0;
      const int me = exposed && m.edge_light != 0
          ? LineVisibility(minute_shadows.edge, m_along, m_across, second_occlusion) : 0;
      const int mc = exposed && m.cap_light != 0
          ? LineVisibility(minute_shadows.cap, m_across + kMinuteWidth, m_along, second_occlusion) : 0;
      const int second_coverage = Mul(second_level,
          detail::ShadowSecondCoverage(s_along, s_across));
      int value = Shade(h, m, he, hc, me, mc, second_body, second_coverage);
      // Emitters lie inside the marker ring; marker shadows stay in this border.
      const int marker_mask = (y < kMarkerBand ? 1 : 0) |
          (x >= pixoo::kWidth - kMarkerBand ? 2 : 0) |
          (y >= pixoo::kHeight - kMarkerBand ? 4 : 0) |
          (x < kMarkerBand ? 8 : 0);
      if (marker_mask != 0) {
        const int marked_he = MarkerVisibility(hour_shadows.edge, h_along, h_across,
                                                marker_mask, he, second_occlusion);
        const int marked_hc = MarkerVisibility(hour_shadows.cap, h_across + kHourWidth,
                                                h_along, marker_mask, hc, second_occlusion);
        const int marked_me = MarkerVisibility(minute_shadows.edge, m_along, m_across,
                                                marker_mask, me, second_occlusion);
        const int marked_mc = MarkerVisibility(minute_shadows.cap, m_across + kMinuteWidth,
                                                m_along, marker_mask, mc, second_occlusion);
        value = Shade(h, m, marked_he, marked_hc, marked_me, marked_mc,
                      second_body, second_coverage);
        for (int marker = 0; marker < kMarkerCount; ++marker) {
          if ((marker_mask & (1 << marker)) == 0)
            continue;
          const int coverage = MarkerCoverage(marker, (2 * x - 63) * 128, dy * 128);
          if (coverage != 0) {
            const int incident = Screen(
                Screen(Mul(h.edge_light, he), Mul(h.cap_light, hc)),
                Screen(Mul(m.edge_light, me), Mul(m.cap_light, mc)));
            const int tone = Mul(kMarkerResponse, incident);
            value = Mul(value, 255 - coverage) + Mul(tone, coverage);
          }
        }
      }
      *row++ = static_cast<uint8_t>(value);
      ah += 2 * hour.x;
      sh -= 2 * hour.y;
      am += 2 * minute.x;
      sm -= 2 * minute.y;
      as += 2 * second.x;
      ss -= 2 * second.y;
    }
  }
}

}  // namespace

// The display and its active canvas belong exclusively to the calling task.
// Only immutable scene data and disjoint grayscale rows cross task boundaries.
struct ShadowFace::RenderState {
#ifdef SHADOW_PARALLEL
  struct Job {
    const SceneSnapshot *scene;
    int first_row, last_row;
  };
  uint8_t *gray{nullptr};
  StaticQueue_t queue_storage;
  uint8_t job_storage[sizeof(Job)];
  StaticSemaphore_t done_storage;
  QueueHandle_t queue{nullptr};
  SemaphoreHandle_t done{nullptr};
  TaskHandle_t task{nullptr};

  static void Worker(void *arg) {
    auto *self = static_cast<RenderState *>(arg);
    for (;;) {
      Job job;
      xQueueReceive(self->queue, &job, portMAX_DELAY);
      if (job.scene == nullptr) {
        // Nothing may access self after this acknowledgement: the owner can
        // release it immediately, while task deletion finishes on this core.
        const auto done = self->done;
        xSemaphoreGive(done);
        vTaskDelete(nullptr);
        return;
      }
      RenderRows(*job.scene, job.first_row, job.last_row,
                 self->gray + job.first_row * 64, 2);
      xSemaphoreGive(self->done);
    }
  }

  bool Start() {
    this->gray = static_cast<uint8_t *>(
        heap_caps_malloc(64 * 64, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (this->gray == nullptr)
      this->gray = static_cast<uint8_t *>(
          heap_caps_malloc(64 * 64, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    if (this->gray == nullptr)
      return false;
    this->queue = xQueueCreateStatic(1, sizeof(Job), this->job_storage,
                                    &this->queue_storage);
    this->done = xSemaphoreCreateBinaryStatic(&this->done_storage);
    if (this->queue == nullptr || this->done == nullptr)
      return false;
    // The ordinary ESP-IDF task allocator keeps the stack and TCB in internal
    // RAM. They outlive StopAck and are reclaimed by task deletion, not by us.
    // ESP-IDF task stack sizes are bytes, unlike vanilla FreeRTOS.
    return xTaskCreatePinnedToCore(&RenderState::Worker, "shadow_rows", 2048,
                                  this, 1, &this->task,
                                  1 - xPortGetCoreID()) == pdPASS;
  }

  ~RenderState() {
    if (this->task != nullptr) {
      const Job stop{nullptr, 0, 0};
      xQueueSend(this->queue, &stop, portMAX_DELAY);
      xSemaphoreTake(this->done, portMAX_DELAY);
    }
    if (this->queue != nullptr)
      vQueueDelete(this->queue);
    if (this->done != nullptr)
      vSemaphoreDelete(this->done);
    heap_caps_free(this->gray);
  }
#endif
};

ShadowFace::ShadowFace() = default;
ShadowFace::~ShadowFace() = default;

void ShadowFace::ReleaseRenderResources() {
  this->render_state_.reset();
  this->render_state_attempted_ = false;
}

void ShadowFace::RenderStateDeleter::operator()(RenderState *state) const {
#ifdef SHADOW_PARALLEL
  if (state != nullptr) {
    state->~RenderState();
    heap_caps_free(state);
  }
#else
  delete state;
#endif
}

void ShadowFace::Render(display::Display &display) const {
  const auto levels = this->model_.Levels();
  if (!this->model_.HasTime() || this->model_.Level() == 0.0f) {
    display.fill(Color(0, 0, 0));
    return;
  }
  static_assert(pixoo::kWidth == 64 && pixoo::kHeight == 64);
  auto *canvas = content::BlendCanvasOf(display);
  const SceneSnapshot scene(this->model_.Angles(), levels);
#ifdef SHADOW_PARALLEL
  if (!this->render_state_attempted_) {
    this->render_state_attempted_ = true;
    void *memory = heap_caps_malloc(sizeof(RenderState), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (memory != nullptr) {
      this->render_state_.reset(new (memory) RenderState{});
      if (!this->render_state_->Start())
        this->render_state_.reset();
    }
    if (!this->render_state_)
      ESP_LOGW("shadow_face", "Parallel renderer unavailable; using exact serial rendering");
  }
  if (this->render_state_) {
    auto &state = *this->render_state_;
    // Alternating rows keep directional light fields balanced across cores.
    const RenderState::Job job{&scene, 0, 64};
    xQueueSend(state.queue, &job, portMAX_DELAY);
    RenderRows(scene, 1, 64, state.gray + 64, 2);
    xSemaphoreTake(state.done, portMAX_DELAY);
    if (canvas != nullptr) {
      for (int y = 0; y < 64; ++y)
        canvas->WriteGrayscaleSpan(0, y, 64, state.gray + y * 64);
      return;
    }
    display.fill(Color(0, 0, 0));
    for (int y = 0; y < 64; ++y)
      for (int x = 0; x < 64; ++x) {
        const uint8_t gray = state.gray[y * 64 + x];
        if (gray != 0)
          display.draw_pixel_at(x, y, Color(gray, gray, gray));
      }
    return;
  }
#elif defined(USE_ESP32)
  if (!this->render_state_attempted_) {
    this->render_state_attempted_ = true;
    ESP_LOGW("shadow_face", "Single-core target; using exact serial rendering");
  }
#endif
  if (canvas == nullptr)
    display.fill(Color(0, 0, 0));
  uint8_t row[64];
  for (int y = 0; y < 64; ++y) {
    RenderRows(scene, y, y + 1, row);
    if (canvas != nullptr) {
      canvas->WriteGrayscaleSpan(0, y, 64, row);
    } else {
      for (int x = 0; x < 64; ++x)
        if (row[x] != 0)
          display.draw_pixel_at(x, y, Color(row[x], row[x], row[x]));
    }
  }
}

}  // namespace esphome::pixoo64::clockface

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC pop_options
#endif

#undef SHADOW_PARALLEL
