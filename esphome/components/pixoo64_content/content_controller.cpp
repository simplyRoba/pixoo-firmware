#include "content_controller.h"

#include <algorithm>
#include <cmath>
#include <new>
#include <utility>

#ifdef ESP_PLATFORM
#include "esp_heap_caps.h"
#endif

#include "esphome/core/hal.h"
#include "esphome/core/log.h"

namespace esphome::pixoo64::content {
namespace {

static const char *const TAG = "pixoo64.content";

}  // namespace

void ContentController::add_dashboard(Dashboard *dashboard) {
  this->dashboards_.push_back(dashboard);
}

void ContentController::set_default_dashboard(std::string id) {
  this->default_dashboard_id_ = std::move(id);
}

std::vector<std::string> ContentController::dashboard_ids() const {
  std::vector<std::string> ids;
  ids.reserve(this->dashboards_.size());
  for (const Dashboard *dashboard : this->dashboards_)
    ids.push_back(dashboard->id());
  return ids;
}

bool ContentController::ResolveDashboard(
    const std::string &requested_id, pixoo::DashboardSelection *selection) {
  if (selection == nullptr)
    return false;

  Dashboard *dashboard = this->find_(requested_id);
  if (dashboard == nullptr)
    dashboard = this->find_(this->default_dashboard_id_);
  if (dashboard == nullptr) {
    ESP_LOGW(TAG, "unknown dashboard ID: %s", requested_id.c_str());
    return false;
  }

  *selection = pixoo::DashboardSelection{dashboard->id(),
                                         dashboard->requires_microphone(),
                                         dashboard->frame_interval_ms()};
  if (dashboard->id() != requested_id)
    ESP_LOGW(TAG, "unknown dashboard ID: %s, using default: %s",
             requested_id.c_str(), dashboard->id().c_str());
  return true;
}

void ContentController::SetLevels(const float levels[pixoo::kBands]) {
  for (Dashboard *dashboard : this->dashboards_) {
    if (pixoo::EqualizerLevelsSink *sink = dashboard->levels_sink())
      sink->SetLevels(levels);
  }
}

pixoo::FrameView ContentController::RenderBootAnimation(uint32_t elapsed_ms) {
  constexpr int kSize = pixoo::kWidth;         // 64
  constexpr int kPerimeter = 4 * kSize;        // 256, continuous around the ring
  constexpr int kSpotCount = 6;
  constexpr int kGlowDepth = 21;              // feathered band, leaves a dark core
  constexpr int kBlurRadius = 5;               // smooths the moving color field
  constexpr uint32_t kBreathePeriodMs = 2000;  // whole ring brightens and dims
  // Each color pool has its own hue, size, drift speed and direction so the ring
  // churns unevenly instead of marching in lockstep.
  struct Spot {
    pixoo::Rgb color;
    int radius;
    int start;         // ring position at t=0
    uint32_t period;   // ms for one full lap
    int dir;           // +1 clockwise, -1 counter
  };
  static const Spot kSpots[kSpotCount] = {
      {{0x22, 0xDD, 0xFF}, 80, 0, 5200, +1},    // cyan
      {{0x2A, 0x70, 0xFF}, 58, 40, 3300, -1},   // blue
      {{0x8B, 0x5C, 0xFF}, 92, 96, 6100, +1},   // violet
      {{0xE2, 0x45, 0xD6}, 62, 150, 2500, -1},  // magenta
      {{0xFF, 0x72, 0x9C}, 84, 200, 4600, +1},  // warm pink
      {{0x30, 0xF0, 0xC8}, 54, 224, 3800, -1},  // teal accent
  };

  // Build a continuous color and strength field around the ring. Spots use a
  // squared falloff so their pools fade smoothly rather than as sharp triangles.
  pixoo::Rgb raw_colors[kPerimeter];
  int raw_strengths[kPerimeter];
  int spot_centers[kSpotCount];
  for (int i = 0; i < kSpotCount; ++i) {
    int travel = static_cast<int>((elapsed_ms % kSpots[i].period) * kPerimeter /
                                  kSpots[i].period);
    if (kSpots[i].dir < 0)
      travel = kPerimeter - travel;
    spot_centers[i] = (kSpots[i].start + travel) % kPerimeter;
  }
  for (int position = 0; position < kPerimeter; ++position) {
    int red = 0;
    int green = 0;
    int blue = 0;
    int total_weight = 0;
    int strongest_weight = 0;
    for (int i = 0; i < kSpotCount; ++i) {
      int distance = position - spot_centers[i];
      if (distance < 0)
        distance = -distance;
      if (distance > kPerimeter / 2)
        distance = kPerimeter - distance;
      if (distance >= kSpots[i].radius)
        continue;
      const int reach = kSpots[i].radius - distance;
      const int weight =
          reach * reach * 255 / (kSpots[i].radius * kSpots[i].radius);
      red += kSpots[i].color.r * weight;
      green += kSpots[i].color.g * weight;
      blue += kSpots[i].color.b * weight;
      total_weight += weight;
      strongest_weight = std::max(strongest_weight, weight);
    }
    if (total_weight > 0) {
      raw_colors[position] =
          pixoo::Rgb{static_cast<uint8_t>(red / total_weight),
                     static_cast<uint8_t>(green / total_weight),
                     static_cast<uint8_t>(blue / total_weight)};
    } else {
      raw_colors[position] = pixoo::Rgb{};
    }
    raw_strengths[position] = strongest_weight;
  }

  // Box-blur the strength field around the ring so the inner edge of the glow is
  // soft, then ride a couple of traveling ripples over it so the brightness
  // wells and thins unevenly like a Siri glow instead of a steady band.
  const int ripple_a = static_cast<int>(elapsed_ms * 360 / 1500);
  const int ripple_b = static_cast<int>(elapsed_ms * 360 / 850);
  int strengths[kPerimeter];
  for (int position = 0; position < kPerimeter; ++position) {
    int sum = 0;
    for (int d = -kBlurRadius; d <= kBlurRadius; ++d) {
      int sample = (position + d) % kPerimeter;
      if (sample < 0)
        sample += kPerimeter;
      sum += raw_strengths[sample];
    }
    int value = sum / (2 * kBlurRadius + 1);
    const int w1 = cos_deg_(position * 7 + ripple_a);
    const int w2 = cos_deg_(position * 13 - ripple_b);
    const int turbulence = 150 + (w1 + w2 + 510) * 105 / 1020;  // 150..255
    strengths[position] = value * turbulence / 255;
  }

  // Slow global breathing keeps the ring alive without the spots ever fully
  // vanishing (192..255 of full).
  const int phase = static_cast<int>((elapsed_ms % kBreathePeriodMs) * 360 /
                                     kBreathePeriodMs);
  const int breathe = 192 + 63 * (cos_deg_(phase) + 255) / 510;

  this->framebuffer_.Clear();
  for (int y = 0; y < kSize; ++y) {
    for (int x = 0; x < kSize; ++x) {
      const int d_top = y;
      const int d_bottom = kSize - 1 - y;
      const int d_left = x;
      const int d_right = kSize - 1 - x;
      const int edge_dist = std::min(std::min(d_top, d_bottom),
                                     std::min(d_left, d_right));
      if (edge_dist >= kGlowDepth)
        continue;

      // Every nearby edge contributes. A corner pixel is lit by two edges at
      // once, so the glow blooms around the corner rather than leaving a seam.
      int color_r = 0;
      int color_g = 0;
      int color_b = 0;
      int level_sum = 0;
      const auto add_edge = [&](int depth, int position) {
        if (depth >= kGlowDepth)
          return;
        const int reach = kGlowDepth - depth;
        const int profile = reach * reach * 255 / (kGlowDepth * kGlowDepth);
        int level = strengths[position] * profile / 255;
        level = level * breathe / 255;
        if (level <= 0)
          return;
        const pixoo::Rgb c = raw_colors[position];
        color_r += c.r * level;
        color_g += c.g * level;
        color_b += c.b * level;
        level_sum += level;
      };
      add_edge(d_top, x);
      add_edge(d_right, kSize + y);
      add_edge(d_bottom, 2 * kSize + (kSize - 1 - x));
      add_edge(d_left, 3 * kSize + (kSize - 1 - y));
      if (level_sum <= 0)
        continue;

      // Hue is the level-weighted blend of the contributing edges; brightness is
      // their sum (clamped), so corners get brighter without washing to white.
      const int bright = std::min(level_sum, 255);
      this->framebuffer_.SetPixel(
          x, y,
          pixoo::Rgb{
              static_cast<uint8_t>(color_r / level_sum * bright / 255),
              static_cast<uint8_t>(color_g / level_sum * bright / 255),
              static_cast<uint8_t>(color_b / level_sum * bright / 255)});
    }
  }

  DrawBootWordmark(elapsed_ms);

  const auto &payload = this->framebuffer_.payload();
  return pixoo::FrameView{payload.data(), payload.size()};
}

// Minimalist "PIXOO" / "64" wordmark, centered in the dark core, fading up on
// boot. Kept as a compact built-in 5x7 font so the animation has no font deps.
void ContentController::DrawBootWordmark(uint32_t elapsed_ms) {
  constexpr int kSize = pixoo::kWidth;
  constexpr int kGlyphW = 5;
  constexpr int kGlyphH = 7;
  constexpr int kGap = 1;
  static const uint8_t kPixoo[5][kGlyphH] = {
      {0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10},  // P
      {0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x1F},  // I
      {0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11},  // X
      {0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E},  // O
      {0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E},  // O
  };
  static const uint8_t kSixtyFour[2][kGlyphH] = {
      {0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E},  // 6
      {0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02},  // 4
  };

  // Fade the text in over the first stretch of the boot, then hold.
  const int gain = static_cast<int>(std::min<uint32_t>(elapsed_ms, 600) * 255 /
                                    600);
  if (gain <= 0)
    return;
  const pixoo::Rgb ink{static_cast<uint8_t>(226 * gain / 255),
                       static_cast<uint8_t>(232 * gain / 255),
                       static_cast<uint8_t>(245 * gain / 255)};

  const auto blit = [&](const uint8_t (*glyphs)[kGlyphH], int count, int gap,
                        int y0) {
    const int word_w = count * (kGlyphW + gap) - gap;
    const int x0 = (kSize - word_w) / 2;
    for (int i = 0; i < count; ++i) {
      const int lx = x0 + i * (kGlyphW + gap);
      for (int gy = 0; gy < kGlyphH; ++gy) {
        for (int gx = 0; gx < kGlyphW; ++gx) {
          if (glyphs[i][gy] & (1 << (kGlyphW - 1 - gx)))
            this->framebuffer_.SetPixel(lx + gx, y0 + gy, ink);
        }
      }
    }
  };

  // Two centered rows: PIXOO over 64. The wider top row holds more lit pixels,
  // so nudge the whole block down until the ink weight sits on the panel center
  // rather than geometrically centering the bounding box (which reads top-heavy).
  constexpr int kRowGap = 3;
  constexpr int kBlockH = 2 * kGlyphH + kRowGap;
  constexpr int kWeightOffset = 3;
  const int top = (kSize - kBlockH) / 2 + kWeightOffset;
  blit(kPixoo, 5, kGap, top);
  blit(kSixtyFour, 2, 2, top + kGlyphH + kRowGap);
}

// Cosine of an angle in whole degrees, scaled to [-255, 255]. Small integer
// table keeps the boot animation free of floating point.
int ContentController::cos_deg_(int degrees) {
  static const int8_t kQuarter[91] = {
      127, 127, 127, 127, 127, 127, 126, 126, 126, 125, 125, 124, 123, 123,
      122, 121, 120, 119, 118, 117, 116, 115, 114, 112, 111, 110, 108, 107,
      105, 104, 102, 100, 99,  97,  95,  93,  91,  89,  87,  85,  83,  81,
      79,  76,  74,  72,  70,  67,  65,  62,  60,  58,  55,  53,  50,  48,
      45,  42,  40,  37,  34,  32,  29,  26,  24,  21,  18,  15,  13,  10,
      7,   4,   2,   -1,  -4,  -6,  -9,  -12, -15, -17, -20, -23, -26, -28,
      -31, -34, -36, -39, -42, -44, -47};
  degrees %= 360;
  if (degrees < 0)
    degrees += 360;
  int value;
  if (degrees <= 90)
    value = kQuarter[degrees];
  else if (degrees <= 180)
    value = -kQuarter[180 - degrees];
  else if (degrees <= 270)
    value = -kQuarter[degrees - 180];
  else
    value = kQuarter[360 - degrees];
  return value * 255 / 127;
}

pixoo::FrameView ContentController::RenderFirmwareUpdate() {
  const Color kMark(0x28, 0x1B, 0x00);
  const Color kAmber(0xFF, 0xB3, 0x00);
  const Color kWhite(0xF4, 0xF4, 0xF4);

  this->framebuffer_.Clear();

  // The shape and lighting are mirrored around x=31.5. Color changes on every
  // row and across every row: pale at the top-center, amber at the lower edges.
  for (int y = 4; y <= 28; ++y) {
    const int half_width = (y - 4) * 14 / 24;
    const int vertical = (y - 4) * 255 / 24;
    for (int x = 31 - half_width; x <= 32 + half_width; ++x) {
      const int doubled_x = x * 2;
      const int center_distance =
          doubled_x < 63 ? 63 - doubled_x : doubled_x - 63;
      const int edge = center_distance * 255 / 29;
      const Color fill(0xFF - edge * 20 / 255,
                       0xF5 - vertical * 75 / 255 - edge * 60 / 255,
                       0xA0 - vertical * 125 / 255 - edge * 35 / 255);
      this->draw_pixel_at(x, y, fill);
    }
  }

  this->filled_rectangle(30, 12, 4, 7, kMark);
  this->filled_rectangle(30, 22, 4, 3, kMark);

  if (this->firmware_update_title_font_ != nullptr) {
    this->print(32, 29, this->firmware_update_title_font_, kAmber,
                display::TextAlign::TOP_CENTER, "FLASHING");
  }
  if (this->firmware_update_detail_font_ != nullptr) {
    this->print(32, 50, this->firmware_update_detail_font_, kWhite,
                display::TextAlign::TOP_CENTER, "KEEP POWER ON");
  }

  const auto &payload = this->framebuffer_.payload();
  return pixoo::FrameView{payload.data(), payload.size()};
}

uint32_t ContentController::NotificationMinVisibleMs(
    const pixoo::Notification &notification) {
  return this->notification_renderer_.ScrollPassMs(notification);
}

void ContentController::HideBaseContent() {
  this->visible_ = nullptr;
  this->ReleaseReactionBackground_();
}

void ContentController::ReleaseOverlayResources() {
  this->ReleaseReactionBackground_();
}

void ContentController::RecordRender_(uint32_t elapsed_us) {
  ++this->render_frames_;
  this->render_total_us_ += elapsed_us;
  this->render_max_us_ = std::max(this->render_max_us_, elapsed_us);
  if (elapsed_us > this->render_budget_us_)
    ++this->render_over_budget_;
}

void ContentController::PublishRenderWindow_() {
  const float average_ms =
      this->render_frames_ == 0
          ? 0.0f
          : static_cast<float>(this->render_total_us_) /
                static_cast<float>(this->render_frames_) / 1000.0f;
  if (this->render_average_sensor_ != nullptr)
    this->render_average_sensor_->publish_state(average_ms);
  if (this->render_max_sensor_ != nullptr)
    this->render_max_sensor_->publish_state(this->render_max_us_ / 1000.0f);
  if (this->render_over_budget_sensor_ != nullptr)
    this->render_over_budget_sensor_->publish_state(
        static_cast<float>(this->render_over_budget_));
}

void ContentController::ResetRenderWindow_() {
  this->render_frames_ = 0;
  this->render_over_budget_ = 0;
  this->render_max_us_ = 0;
  this->render_total_us_ = 0;
}

void ContentController::update() {
  if (this->render_budget_us_ == 0)
    return;
  this->PublishRenderWindow_();
  this->ResetRenderWindow_();
}

void ContentController::ReactionBackgroundDeleter::operator()(
    pixoo::Framebuffer *framebuffer) const {
#ifdef ESP_PLATFORM
  framebuffer->~Framebuffer();
  heap_caps_free(framebuffer);
#else
  delete framebuffer;
#endif
}

bool ContentController::EnsureReactionBackground_() {
  if (this->reaction_background_ != nullptr)
    return true;
  if (this->reaction_background_allocation_attempted_)
    return false;

  this->reaction_background_allocation_attempted_ = true;
#ifdef ESP_PLATFORM
  void *memory = heap_caps_malloc(sizeof(pixoo::Framebuffer),
                                 MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  if (memory == nullptr) {
    ESP_LOGE(TAG, "reaction background allocation failed");
    return false;
  }
  this->reaction_background_.reset(new (memory) pixoo::Framebuffer{});
#else
  this->reaction_background_.reset(new (std::nothrow) pixoo::Framebuffer{});
  if (this->reaction_background_ == nullptr) {
    ESP_LOGE(TAG, "reaction background allocation failed");
    return false;
  }
#endif
  return true;
}

void ContentController::ReleaseReactionBackground_() {
  this->reaction_background_.reset();
  this->reaction_background_allocation_attempted_ = false;
  this->reaction_snapshot_active_ = false;
  this->reaction_overlay_ = nullptr;
  this->last_reaction_elapsed_ms_ = 0;
}

bool ContentController::CaptureReactionBackground_() {
  if (!this->EnsureReactionBackground_())
    return false;

  constexpr int kBlurRadius = 3;
  constexpr int kDarkenPercent = 42;
  pixoo::Framebuffer &background = *this->reaction_background_;
  const auto &source = this->framebuffer_.payload();

  // A box blur separates into horizontal and vertical passes. This produces
  // the same 7x7 kernel with 14 samples per output pixel instead of 49, keeping
  // the one-time reaction capture comfortably below the frame budget.
  for (int y = 0; y < pixoo::kHeight; ++y) {
    for (int x = 0; x < pixoo::kWidth; ++x) {
      uint32_t channels[3]{};
      for (int dx = -kBlurRadius; dx <= kBlurRadius; ++dx) {
        const int sx = std::max(0, std::min(pixoo::kWidth - 1, x + dx));
        const size_t offset = (y * pixoo::kWidth + sx) * 3;
        for (int channel = 0; channel < 3; ++channel)
          channels[channel] += source[offset + channel];
      }
      background.SetPixel(
          x, y,
          pixoo::Rgb{static_cast<uint8_t>(channels[0] / 7),
                     static_cast<uint8_t>(channels[1] / 7),
                     static_cast<uint8_t>(channels[2] / 7)});
    }
  }

  const auto &horizontal = background.payload();
  for (int y = 0; y < pixoo::kHeight; ++y) {
    for (int x = 0; x < pixoo::kWidth; ++x) {
      uint32_t channels[3]{};
      for (int dy = -kBlurRadius; dy <= kBlurRadius; ++dy) {
        const int sy = std::max(0, std::min(pixoo::kHeight - 1, y + dy));
        const size_t offset = (sy * pixoo::kWidth + x) * 3;
        for (int channel = 0; channel < 3; ++channel)
          channels[channel] += horizontal[offset + channel];
      }
      this->framebuffer_.SetPixel(
          x, y,
          pixoo::Rgb{
              static_cast<uint8_t>(channels[0] * kDarkenPercent / 700),
              static_cast<uint8_t>(channels[1] * kDarkenPercent / 700),
              static_cast<uint8_t>(channels[2] * kDarkenPercent / 700)});
    }
  }
  background = this->framebuffer_;
  return true;
}

bool ContentController::RenderContent(
    uint32_t now_ms, const std::string &dashboard_id,
    const pixoo::StopwatchSnapshot &stopwatch, const pixoo::Overlay *overlay,
    uint32_t overlay_visible_elapsed_ms,
    bool base_visible, bool base_frozen, bool render_base, bool render_overlay,
    pixoo::FrameView *frame) {
  if (frame == nullptr)
    return false;
  const uint32_t started_us =
      this->render_budget_us_ == 0 ? 0 : micros();

  // This renderer owns the framebuffer, so effects may composite against it
  // for the duration of the frame.
  PushActiveBlendCanvas(*this, *this);
  const bool reaction = base_frozen && overlay != nullptr &&
                        overlay->tag == pixoo::OverlayTag::kReaction;
  if (!reaction && this->reaction_snapshot_active_)
    this->ReleaseReactionBackground_();

  // A base that is not drawn is not visible, so returning to it is an entry.
  if (!base_visible)
    this->visible_ = nullptr;
  if (base_visible && render_base) {
    this->framebuffer_.Clear();
    if (this->stopwatch_dashboard_ != nullptr)
      this->stopwatch_dashboard_->set_stopwatch(stopwatch);
    Dashboard *dashboard = this->find_(dashboard_id);
    if (dashboard != nullptr) {
      if (dashboard != this->visible_) {
        this->visible_ = dashboard;
        dashboard->OnShow(now_ms);
      }
      dashboard->Tick(now_ms);
      if (dashboard->available())
        dashboard->Render(*this);
    }
  } else if (!base_visible) {
    this->framebuffer_.Clear();
  }

  if (reaction) {
    const bool new_reaction =
        !this->reaction_snapshot_active_ || overlay != this->reaction_overlay_ ||
        (overlay_visible_elapsed_ms == 0 && this->last_reaction_elapsed_ms_ != 0);
    if (new_reaction) {
      // FirmwareApp has just rendered the clean dashboard for visible-base
      // reactions. An off-state reaction deliberately starts from black.
      if (base_visible)
        this->CaptureReactionBackground_();
      else
        this->framebuffer_.Clear();
      this->reaction_snapshot_active_ = true;
      this->reaction_overlay_ = overlay;
    }
    this->last_reaction_elapsed_ms_ = overlay_visible_elapsed_ms;
    if (render_overlay) {
      // Restoring every frame prevents transformed artwork from leaving stale
      // pixels as it moves. If allocation failed, a black fallback remains
      // valid without dereferencing absent background storage.
      if (this->reaction_background_ != nullptr)
        this->framebuffer_ = *this->reaction_background_;
      else
        this->framebuffer_.Clear();
      this->reaction_renderer_.Render(*this, overlay->reaction,
                                      overlay_visible_elapsed_ms);
    }
  } else if (render_overlay && overlay != nullptr &&
             overlay->tag == pixoo::OverlayTag::kNotification &&
             this->notification_renderer_.ready()) {
    // The opaque banner replaces every pixel it touched on the preceding
    // notification frame. Pixels below it retain the latest base render.
    this->notification_renderer_.Render(*this, overlay->notification,
                                        overlay_visible_elapsed_ms);
  }

  PopActiveBlendCanvas();
  if (this->render_budget_us_ != 0)
    this->RecordRender_(micros() - started_us);
  const auto &payload = this->framebuffer_.payload();
  *frame = pixoo::FrameView{payload.data(), payload.size()};
  return true;
}

// A face composites thousands of pixels per frame, so both pixel paths resolve
// the target byte once and write it in place. They do not call Display::clip(),
// whose bounds test runs through virtual get_width()/get_height() on a geometry
// this renderer knows statically; the clipping rectangle it also checks is set
// only while the notification banner draws, so it is consulted only then.
void ContentController::draw_pixel_at(int x, int y, Color color) {
  uint8_t *pixel = this->framebuffer_.PixelBytes(x, y);
  if (pixel == nullptr)
    return;
  if (this->is_clipping() && !this->get_clipping().inside(x, y))
    return;
  pixel[0] = color.r;
  pixel[1] = color.g;
  pixel[2] = color.b;
}

void ContentController::BlendPixel(int x, int y, Color color, float alpha) {
  if (alpha <= 0.0f)
    return;
  uint8_t *pixel = this->framebuffer_.PixelBytes(x, y);
  if (pixel == nullptr)
    return;
  if (this->is_clipping() && !this->get_clipping().inside(x, y))
    return;
  if (alpha >= 1.0f) {
    pixel[0] = color.r;
    pixel[1] = color.g;
    pixel[2] = color.b;
    return;
  }
  const auto mix = [alpha](uint8_t under, uint8_t over) {
    return static_cast<uint8_t>(std::fma(
        static_cast<float>(over) - static_cast<float>(under), alpha,
        static_cast<float>(under)));
  };
  pixel[0] = mix(pixel[0], color.r);
  pixel[1] = mix(pixel[1], color.g);
  pixel[2] = mix(pixel[2], color.b);
}

Dashboard *ContentController::find_(const std::string &id) const {
  for (Dashboard *dashboard : this->dashboards_) {
    if (dashboard->id() == id)
      return dashboard;
  }
  return nullptr;
}

}  // namespace esphome::pixoo64::content
