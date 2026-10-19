#include "shadow_face.h"

#include <array>
#include <cmath>
#include <limits>
#include <new>

#include "pixoo_cmd.h"

#if defined(USE_ESP32)
#include "esp_attr.h"
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
#define SHADOW_HOT IRAM_ATTR
#define SHADOW_DATA DRAM_ATTR
#else
#define SHADOW_HOT
#define SHADOW_DATA
#endif

// The pixel shader favors execution speed over flash size.
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC push_options
#pragma GCC optimize("O3")
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
constexpr int kSecondStrokeWidth = kOne / 2;
constexpr int kSecondInk = detail::kShadowSecondInk;

constexpr int ClampByte(int value) {
  return value < 0 ? 0 : (value > 255 ? 255 : value);
}
constexpr int Abs(int value) { return value < 0 ? -value : value; }
constexpr int Max(int a, int b) { return a > b ? a : b; }
constexpr int Min(int a, int b) { return a < b ? a : b; }
constexpr int Mul(int a, int b) { return (a * b + 127) / 255; }
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
SHADOW_DATA constexpr auto kEmissionNear = MakeScaledEmission(130);
SHADOW_DATA constexpr auto kEmissionDiffuse = MakeScaledEmission(120);
SHADOW_DATA constexpr auto kEmissionCrease = MakeScaledEmission(245);

constexpr std::array<uint32_t, 512> MakeReciprocal() {
  std::array<uint32_t, 512> result{};
  for (uint32_t i = 1; i < result.size(); ++i)
    result[i] = 16711680u / i;
  return result;
}
SHADOW_DATA constexpr auto kReciprocal = MakeReciprocal();

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
SHADOW_DATA constexpr auto kFrontSpans = MakeFrontSpans();

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
  std::array<EdgeBlocker, 2> bodies;
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
    for (int body = 0; body < 2; ++body) {
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
  const Point a = Sub(body[start], q);
  const Point b = Sub(body[(last + 1) % 4], q);
  const int sign = Cross(a, b) >= 0 ? 1 : -1;
  return interval.Ray(a, q, offset, sign) &&
         interval.Ray(b, q, offset, -sign) &&
         blocker.ClipFace(start, interval) &&
         (last == start || blocker.ClipFace(last, interval));
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

int SHADOW_HOT LineVisibility(const LineSource &shadows, int along, int across) {
  std::array<Interval, 2> blocked;
  int count = 0;
  for (const auto &body : shadows.bodies) {
    if (BlockedInterval(body, {along, across}, shadows.length,
                        shadows.edge_offset, blocked[count]))
      ++count;
  }
  if (count == 0)
    return 255;
  if (count == 2) {
    if (blocked[1].lo < blocked[0].lo) {
      const Interval first = blocked[0];
      blocked[0] = blocked[1];
      blocked[1] = first;
    }
    if (blocked[1].lo <= blocked[0].hi) {
      blocked[0].hi = Max(blocked[0].hi, blocked[1].hi);
      count = 1;
    }
  }
  if (count == 1 && blocked[0].lo == 0 && blocked[0].hi == shadows.length)
    return 0;
  const int normal = Max(Abs(across - shadows.edge_offset), kOne);
  const int end_integral = EdgeIntegral(shadows.length - along, normal);
  const int start_integral = EdgeIntegral(-along, normal);
  const int full = end_integral - start_integral;
  int covered = 0;
  for (int i = 0; i < count; ++i)
    covered += (blocked[i].hi == shadows.length ? end_integral :
                EdgeIntegral(blocked[i].hi - along, normal)) -
               (blocked[i].lo == 0 ? start_integral :
                EdgeIntegral(blocked[i].lo - along, normal));
  return full > 0 ? ClampByte(255 * (full - covered) / full) : 255;
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
                const Direction &second, bool clip_other = true) {
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
  return Mul(other_gain, second_gain);
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

uint8_t SHADOW_HOT detail::ShadowSecondCoverage(int along, int across) {
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
  int opacity;

  SceneSnapshot(const pixoo::clock::ShadowHandAngles &angles, int opacity)
      : hour(angles.hour), minute(angles.minute), second(angles.second),
        hour_shadows(hour, kHourLength, kHourWidth, 1,
                     minute, kMinuteLength, kMinuteWidth, second),
        minute_shadows(minute, kMinuteLength, kMinuteWidth, -1,
                       hour, kHourLength, kHourWidth, second), opacity(opacity) {}
};

// Output starts at first_row and retains screen-row spacing. Signed division
// retains C++ truncation toward zero for negative subpixel projections.
void SHADOW_HOT RenderRows(const SceneSnapshot &scene, int first_row, int last_row,
                           uint8_t *gray, int row_step = 1) {
  const auto &hour = scene.hour, &minute = scene.minute, &second = scene.second;
  const auto &hour_shadows = scene.hour_shadows;
  const auto &minute_shadows = scene.minute_shadows;
  const int opacity = scene.opacity;
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
            minute, kMinuteLength, kMinuteWidth, second)),
            Mul(h.cap_crease, SurfaceGain(h.cap_crease, {kHourLength, 0}, hour, 1,
            minute, kMinuteLength, kMinuteWidth, second)));
      }
      if (m.crease != 0 || m.cap_crease != 0) {
        // The minute surface is above the hour surface.
        m.crease = Max(Mul(m.crease, SurfaceGain(m.crease, m.edge_surface, minute, -1,
            hour, kHourLength, kHourWidth, second, false)),
            Mul(m.cap_crease, SurfaceGain(m.cap_crease, {kMinuteLength, 0}, minute, -1,
            hour, kHourLength, kHourWidth, second, false)));
      }
      const int s_along = as / 256, s_across = ss / 256;
      const int second_body = BodyCoverage(
          s_along, s_across, kSecondLength, kSecondWidth, kSecondTail);
      const bool exposed = Max(Max(h.body, m.body), second_body) < 255;
      const int he = exposed && h.edge_light != 0
          ? LineVisibility(hour_shadows.edge, h_along, h_across) : 0;
      const int hc = exposed && h.cap_light != 0
          ? LineVisibility(hour_shadows.cap, h_across + kHourWidth, h_along) : 0;
      const int me = exposed && m.edge_light != 0
          ? LineVisibility(minute_shadows.edge, m_along, m_across) : 0;
      const int mc = exposed && m.cap_light != 0
          ? LineVisibility(minute_shadows.cap, m_across + kMinuteWidth, m_along) : 0;
      const int value = Shade(h, m, he, hc, me, mc,
          second_body, detail::ShadowSecondCoverage(s_along, s_across));
      *row++ = static_cast<uint8_t>(Mul(value, opacity));
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
  uint8_t gray[64 * 64];
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
    this->queue = xQueueCreateStatic(1, sizeof(Job), this->job_storage,
                                    &this->queue_storage);
    this->done = xSemaphoreCreateBinaryStatic(&this->done_storage);
    if (this->queue == nullptr || this->done == nullptr)
      return false;
    // The ordinary ESP-IDF task allocator keeps the stack and TCB in internal
    // RAM. They outlive StopAck and are reclaimed by task deletion, not by us.
    // ESP-IDF task stack sizes are bytes, unlike vanilla FreeRTOS.
    return xTaskCreatePinnedToCore(&RenderState::Worker, "shadow_rows", 5120,
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
  }
#endif
};

ShadowFace::ShadowFace() = default;
ShadowFace::~ShadowFace() = default;

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

void SHADOW_HOT ShadowFace::Render(display::Display &display) const {
  const int opacity = static_cast<int>(this->model_.Level() * 255.0f + .5f);
  if (!this->model_.HasTime() || opacity == 0) {
    display.fill(Color(0, 0, 0));
    return;
  }
  static_assert(pixoo::kWidth == 64 && pixoo::kHeight == 64);
  const SceneSnapshot scene(this->model_.Angles(), opacity);
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
  display.fill(Color(0, 0, 0));
  uint8_t row[64];
  for (int y = 0; y < 64; ++y) {
    RenderRows(scene, y, y + 1, row);
    for (int x = 0; x < 64; ++x)
      if (row[x] != 0)
        display.draw_pixel_at(x, y, Color(row[x], row[x], row[x]));
  }
}

}  // namespace esphome::pixoo64::clockface

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC pop_options
#endif

#undef SHADOW_HOT
#undef SHADOW_DATA
#undef SHADOW_PARALLEL
