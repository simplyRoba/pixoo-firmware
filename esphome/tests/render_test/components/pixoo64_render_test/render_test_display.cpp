#include "render_test_display.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <numeric>
#include <utility>

#include "dashboard/weather/weather_icon.h"
#include "esphome/components/pixoo64_content/blend_canvas.h"
#include "png.h"

namespace esphome::pixoo64_render_test {
namespace {

std::vector<uint8_t> ReadFile(const std::string &path) {
  std::ifstream in(path, std::ios::binary);
  return std::vector<uint8_t>((std::istreambuf_iterator<char>(in)),
                              std::istreambuf_iterator<char>());
}

bool WriteFile(const std::string &path, const std::vector<uint8_t> &data) {
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  out.write(reinterpret_cast<const char *>(data.data()), data.size());
  return out.good();
}

}  // namespace

void RenderTestDisplay::setup() {
  this->set_timeout(100, [this]() {
    const bool update = std::getenv("PIXOO_UPDATE_SNAPSHOTS") != nullptr;
    int failures = 0;
    auto check = [&](const std::string &id) {
      const std::vector<uint8_t> png =
          EncodePng(this->framebuffer_.data(), 64, 64);
      const std::string path = this->output_dir_ + "/" + id + ".png";
      if (update) {
        if (!WriteFile(path, png)) {
          std::printf("render test: FAILED to write %s\n", path.c_str());
          ++failures;
        } else {
          std::printf("render test: wrote %s\n", path.c_str());
        }
        return;
      }
      if (ReadFile(path) != png) {
        std::printf("render test: MISMATCH %s\n", path.c_str());
        ++failures;
      } else {
        std::printf("render test: ok %s\n", path.c_str());
      }
    };

    // Feed a strong, varied synthetic spectrum through the renderer sink, which
    // fans it out to every equalizer face. Two rising updates followed by a
    // falling update put the dashboard smoothing in a deterministic state while
    // exercising substantial bass, midrange, and treble displacement.
    if (this->equalizer_ != nullptr) {
      const float hi[pixoo::kBands] = {
          0.70f, 0.82f, 0.62f, 0.90f, 0.74f, 1.00f, 0.68f, 0.88f,
          0.76f, 0.94f, 0.66f, 0.84f, 0.58f, 0.78f, 0.50f, 0.70f,
      };
      float lo[pixoo::kBands];
      for (int b = 0; b < pixoo::kBands; b++)
        lo[b] = hi[b] * 0.35f;
      this->content_controller_->SetLevels(hi);
      this->content_controller_->SetLevels(hi);
      this->content_controller_->SetLevels(lo);
    }

    const auto &dashboard_ids = this->content_controller_->dashboard_ids();
    const bool has_cloud_motion_fixture =
        std::find(dashboard_ids.begin(), dashboard_ids.end(),
                  "weather_landscape_cloud_subpixel") != dashboard_ids.end() &&
        std::find(dashboard_ids.begin(), dashboard_ids.end(),
                  "weather_landscape_cloud_baseline") != dashboard_ids.end();

    // Compare partly cloudy frames to matching cloud-free frames so the sun's
    // pulse cannot satisfy the fractional-motion assertion. The cloud band
    // advances less than one whole pixel across both intervals.
    auto capture_cloud_scene = [&](uint32_t now_ms, const char *dashboard,
                                   std::vector<uint8_t> *frame) {
      if (!this->render_frame_(now_ms, dashboard, nullptr, 0, true))
        return false;
      *frame = this->framebuffer_;
      return true;
    };
    std::vector<uint8_t> cloud_start;
    std::vector<uint8_t> cloud_next;
    std::vector<uint8_t> cloud_prewrap;
    std::vector<uint8_t> cloud_wrap;
    std::vector<uint8_t> clear_start;
    std::vector<uint8_t> clear_next;
    std::vector<uint8_t> clear_prewrap;
    std::vector<uint8_t> clear_wrap;
    const bool cloud_frames_valid =
        !has_cloud_motion_fixture ||
        (capture_cloud_scene(0, "weather_landscape_cloud_subpixel",
                             &cloud_start) &&
         capture_cloud_scene(33, "weather_landscape_cloud_subpixel",
                             &cloud_next) &&
         capture_cloud_scene(32760, "weather_landscape_cloud_subpixel",
                             &cloud_prewrap) &&
         capture_cloud_scene(32776, "weather_landscape_cloud_subpixel",
                             &cloud_wrap) &&
         capture_cloud_scene(0, "weather_landscape_cloud_baseline",
                             &clear_start) &&
         capture_cloud_scene(33, "weather_landscape_cloud_baseline",
                             &clear_next) &&
         capture_cloud_scene(32760, "weather_landscape_cloud_baseline",
                             &clear_prewrap) &&
         capture_cloud_scene(32776, "weather_landscape_cloud_baseline",
                             &clear_wrap));
    bool subpixel_changed = false;
    int wrap_delta = 0;
    if (has_cloud_motion_fixture && cloud_frames_valid) {
      constexpr size_t kCloudBandStart = 4u * 64u * 3u;
      constexpr size_t kCloudBandEnd = 24u * 64u * 3u;
      for (size_t i = kCloudBandStart; i < kCloudBandEnd; i++) {
        const int start_residual =
            static_cast<int>(cloud_start[i]) - clear_start[i];
        const int next_residual =
            static_cast<int>(cloud_next[i]) - clear_next[i];
        subpixel_changed |= start_residual != next_residual;
        const int prewrap_residual =
            static_cast<int>(cloud_prewrap[i]) - clear_prewrap[i];
        const int wrap_residual =
            static_cast<int>(cloud_wrap[i]) - clear_wrap[i];
        wrap_delta =
            std::max(wrap_delta, std::abs(prewrap_residual - wrap_residual));
      }
    }
    if (has_cloud_motion_fixture &&
        (!cloud_frames_valid || !subpixel_changed || wrap_delta > 8)) {
      std::printf("render test: FAILED cloud subpixel motion\n");
      ++failures;
    }

    // The forecast hero icon animates inside a box the layout reserves for it,
    // between the clock above and the statistics below. A loop that reaches
    // outside that box draws over text, and a fixed-time snapshot only catches
    // that at the instant it was taken. The box is the icon's centre plus its
    // half-extent, as the forecast layout places it.
    constexpr int kHeroLeft = 14 - 10;
    constexpr int kHeroRight = 14 + 10;
    constexpr int kHeroTop = 18 - 10;
    constexpr int kHeroBottom = 18 + 10;
    // The icon is drawn on its own, over a blank frame, so what is lit is
    // exactly what it drew: no text is present to be mistaken for it, and an
    // overlap that is there at every instant is caught as readily as one that
    // appears part-way through a loop. The layout's own centre, size, and
    // fixed_time are irrelevant here; only the icon's reach is under test.
    {
      const pixoo::WeatherCondition conditions[] = {
          pixoo::WeatherCondition::SUNNY,
          pixoo::WeatherCondition::PARTLYCLOUDY,
          pixoo::WeatherCondition::CLOUDY,
          pixoo::WeatherCondition::FOG,
          pixoo::WeatherCondition::DRIZZLE,
          pixoo::WeatherCondition::FREEZING_DRIZZLE,
          pixoo::WeatherCondition::RAINY,
          pixoo::WeatherCondition::POURING,
          pixoo::WeatherCondition::FREEZING_RAIN,
          pixoo::WeatherCondition::SNOWY,
          pixoo::WeatherCondition::SNOW_GRAINS,
          pixoo::WeatherCondition::THUNDERSTORM,
          pixoo::WeatherCondition::HAIL_THUNDERSTORM,
          pixoo::WeatherCondition::UNKNOWN,
      };
      // The icon composites through the active blend canvas, so the probe
      // supplies its own and records every pixel either path touches.
      struct ProbeCanvas final : pixoo64::content::BlendCanvas {
        bool touched[pixoo::kHeight][pixoo::kWidth]{};
        void BlendPixel(int x, int y, Color, float alpha) override {
          if (alpha <= 0.0f || x < 0 || x >= pixoo::kWidth || y < 0 ||
              y >= pixoo::kHeight)
            return;
          this->touched[y][x] = true;
        }
      };
      for (pixoo::WeatherCondition condition : conditions) {
        for (int night = 0; night <= 1; night++) {
          bool escaped = false;
          for (uint32_t t = 0; t <= 7000 && !escaped; t += 97) {
            ProbeCanvas probe;
            pixoo64::content::PushActiveBlendCanvas(*this, probe);
            std::fill(this->framebuffer_.begin(), this->framebuffer_.end(), 0u);
            pixoo64::weather::IconAnimation anim{t, 1.0f};
            pixoo64::weather::DrawWeatherIconHero(*this, condition, night != 0,
                                                  14, 18, 10, anim);
            pixoo64::content::PopActiveBlendCanvas();
            for (int y = 0; y < pixoo::kHeight && !escaped; y++) {
              for (int x = 0; x < pixoo::kWidth; x++) {
                if (y >= kHeroTop && y <= kHeroBottom && x >= kHeroLeft &&
                    x <= kHeroRight)
                  continue;
                const size_t i =
                    (static_cast<size_t>(y) * pixoo::kWidth + x) * 3u;
                const bool drawn =
                    probe.touched[y][x] || this->framebuffer_[i] != 0u ||
                    this->framebuffer_[i + 1] != 0u ||
                    this->framebuffer_[i + 2] != 0u;
                if (!drawn)
                  continue;
                std::printf(
                    "render test: FAILED hero icon %d night=%d left its box "
                    "at %ums (%d,%d)\n",
                    static_cast<int>(condition), night, t, x, y);
                ++failures;
                escaped = true;
                break;
              }
            }
          }
        }
      }

      // A mini icon must sit on its own column: whole-pixel and
      // coverage-shaded parts address the grid differently, so a part placed
      // on the wrong one of the two lands half a pixel off the rest and the
      // icon leans. Weight is compared rather than a mirror image, because
      // several subjects are deliberately one-sided (a sun beside a cloud, a
      // bolt with a streak opposite it) while still having to balance.
      constexpr int kMiniCx = 31;
      constexpr int kMiniCy = 51;
      struct MiniCase {
        pixoo::WeatherCondition condition;
        bool night;
        // Columns the subject is intentionally offset by, in whole pixels.
        int allowed_lean;
      };
      const MiniCase mini_cases[] = {
          {pixoo::WeatherCondition::SUNNY, false, 0},
          {pixoo::WeatherCondition::SUNNY, true, 3},
          {pixoo::WeatherCondition::PARTLYCLOUDY, false, 4},
          {pixoo::WeatherCondition::PARTLYCLOUDY, true, 4},
          {pixoo::WeatherCondition::CLOUDY, false, 1},
          {pixoo::WeatherCondition::FOG, false, 0},
          {pixoo::WeatherCondition::DRIZZLE, false, 1},
          {pixoo::WeatherCondition::FREEZING_DRIZZLE, false, 1},
          {pixoo::WeatherCondition::RAINY, false, 1},
          {pixoo::WeatherCondition::POURING, false, 1},
          {pixoo::WeatherCondition::FREEZING_RAIN, false, 1},
          {pixoo::WeatherCondition::SNOWY, false, 0},
          {pixoo::WeatherCondition::SNOW_GRAINS, false, 1},
          {pixoo::WeatherCondition::THUNDERSTORM, false, 2},
          {pixoo::WeatherCondition::HAIL_THUNDERSTORM, false, 2},
          {pixoo::WeatherCondition::UNKNOWN, false, 1},
      };
      for (const MiniCase &mini : mini_cases) {
        std::fill(this->framebuffer_.begin(), this->framebuffer_.end(), 0u);
        pixoo64::weather::DrawWeatherIconMini(*this, mini.condition, mini.night,
                                              kMiniCx, kMiniCy, 4);
        // Centre of mass of the lit pixels, weighted by brightness.
        long weight = 0;
        long moment = 0;
        for (int y = kMiniCy - 8; y <= kMiniCy + 7; y++) {
          for (int x = kMiniCx - 9; x <= kMiniCx + 9; x++) {
            const size_t i = (static_cast<size_t>(y) * pixoo::kWidth + x) * 3u;
            const long lit = this->framebuffer_[i] + this->framebuffer_[i + 1] +
                             this->framebuffer_[i + 2];
            weight += lit;
            moment += lit * (x - kMiniCx);
          }
        }
        if (weight == 0)
          continue;
        // Tenths of a pixel, so a half-pixel lean is unambiguous.
        const long lean = (moment * 10) / weight;
        if (std::labs(lean) > mini.allowed_lean * 10L) {
          std::printf(
              "render test: FAILED mini icon %d night=%d leans %ld.%ld px off "
              "centre (allowed %d)\n",
              static_cast<int>(mini.condition), mini.night ? 1 : 0, lean / 10,
              std::labs(lean) % 10, mini.allowed_lean);
          ++failures;
        }
      }
    }

    for (const std::string &id : dashboard_ids) {
      if (this->has_animation_frames_(id))
        continue;
      if (!this->render_frame_(0, id, nullptr, 0, true)) {
        std::printf("render test: FAILED to render %s\n", id.c_str());
        ++failures;
      } else {
        check(id);
      }
    }

    for (const AnimationFrame &frame : this->animation_frames_) {
      if (!this->render_frame_(frame.now_ms, frame.dashboard_id, nullptr, 0,
                               frame.base_visible)) {
        std::printf("render test: FAILED to render %s at %ums\n",
                    frame.dashboard_id.c_str(), frame.now_ms);
        ++failures;
      } else if (!frame.snapshot_id.empty()) {
        check(frame.snapshot_id);
      }
    }

    const struct {
      const char *id;
      const char *text;
      pixoo::Severity severity;
    } notes[] = {
        {"notify_info", "Info", pixoo::Severity::kInfo},
        {"notify_success", "Saved", pixoo::Severity::kSuccess},
        {"notify_warning", "Door open", pixoo::Severity::kWarning},
        {"notify_error", "Offline", pixoo::Severity::kError},
    };
    for (const auto &n : notes) {
      const pixoo::Notification notification{n.text, n.severity};
      if (!this->render_frame_(0, "text", &notification, 0, true)) {
        std::printf("render test: FAILED to render %s\n", n.id);
        ++failures;
      } else {
        check(n.id);
      }
    }

    auto render_reaction = [&](pixoo::Reaction reaction, uint32_t elapsed_ms,
                               bool reset_base) {
      if (reset_base &&
          !this->render_frame_(0, "clock_binary", nullptr, 0, true))
        return false;
      pixoo::Overlay overlay;
      overlay.tag = pixoo::OverlayTag::kReaction;
      overlay.reaction = reaction;
      pixoo::FrameView frame;
      if (!this->content_controller_->RenderContent(
              elapsed_ms, "clock_binary", &overlay, elapsed_ms, true, true,
              false, true, &frame) ||
          !frame.valid() || frame.size != this->framebuffer_.size())
        return false;
      std::memcpy(this->framebuffer_.data(), frame.data, frame.size);
      return true;
    };

    // At elapsed zero the artwork is transparent, exposing the exact frozen,
    // blurred, darkened base used by every later frame.
    if (!this->render_frame_(0, "clock_binary", nullptr, 0, true)) {
      std::printf("render test: FAILED reaction background base\n");
      ++failures;
    } else {
      const std::vector<uint8_t> clean_base = this->framebuffer_;
      if (!render_reaction(pixoo::Reaction::kLaughing, 0, false) ||
          this->framebuffer_ == clean_base) {
        std::printf("render test: FAILED reaction blur/darken capture\n");
        ++failures;
      } else {
        const uint64_t clean_sum =
            std::accumulate(clean_base.begin(), clean_base.end(), uint64_t{0});
        const uint64_t reaction_sum = std::accumulate(
            this->framebuffer_.begin(), this->framebuffer_.end(), uint64_t{0});
        if (reaction_sum >= clean_sum) {
          std::printf("render test: FAILED reaction background darkening\n");
          ++failures;
        } else {
          check("reaction_background");
        }
      }
    }

    const pixoo::Reaction reactions[] = {
        pixoo::Reaction::kLaughing,   pixoo::Reaction::kLove,
        pixoo::Reaction::kCrying,     pixoo::Reaction::kAngry,
        pixoo::Reaction::kPoop,       pixoo::Reaction::kApprove,
        pixoo::Reaction::kDisapprove, pixoo::Reaction::kCelebrate,
        pixoo::Reaction::kThinking,   pixoo::Reaction::kSurprised,
        pixoo::Reaction::kFire,       pixoo::Reaction::kEyes,
    };
    for (pixoo::Reaction reaction : reactions) {
      const uint32_t duration = pixoo::ReactionVisibleDurationMs(reaction);
      // Reset and enter at zero so the controller captures a clean base, then
      // inspect two non-integer transformed points in the designed motion.
      bool valid = render_reaction(reaction, 0, true);
      const uint32_t early = duration * 7 / 20;
      valid = valid && render_reaction(reaction, early, false);
      if (!valid) {
        std::printf("render test: FAILED reaction %s early\n",
                    pixoo::ReactionName(reaction));
        ++failures;
      } else {
        check(std::string("reaction_") + pixoo::ReactionName(reaction) +
              "_early");
      }
      const uint32_t late = duration * 13 / 20;
      if (!render_reaction(reaction, late, false)) {
        std::printf("render test: FAILED reaction %s late\n",
                    pixoo::ReactionName(reaction));
        ++failures;
      } else {
        check(std::string("reaction_") + pixoo::ReactionName(reaction) +
              "_late");
      }
    }

    // Replacing transformed fullscreen art must leave no pixels from its
    // predecessor. Compare a promoted reaction against the same reaction drawn
    // from a clean base.
    std::vector<uint8_t> fresh_love;
    bool replacement_clean =
        render_reaction(pixoo::Reaction::kLove, 0, true) &&
        render_reaction(pixoo::Reaction::kLove, 700, false);
    if (replacement_clean)
      fresh_love = this->framebuffer_;
    replacement_clean =
        replacement_clean &&
        render_reaction(pixoo::Reaction::kAngry, 0, true) &&
        render_reaction(pixoo::Reaction::kAngry, 700, false) &&
        render_reaction(pixoo::Reaction::kLove, 0, false) &&
        render_reaction(pixoo::Reaction::kLove, 700, false) &&
        this->framebuffer_ == fresh_love;
    if (!replacement_clean) {
      std::printf("render test: FAILED reaction replacement cleanup\n");
      ++failures;
    }

    const pixoo::Notification replacement{"Saved", pixoo::Severity::kSuccess};
    const pixoo::Notification preceding{"Offline", pixoo::Severity::kError};
    pixoo::Overlay replacement_overlay;
    replacement_overlay.tag = pixoo::OverlayTag::kNotification;
    replacement_overlay.notification = replacement;
    bool replacement_valid =
        this->render_frame_(0, "clock_binary", &replacement, 0, true);
    std::vector<uint8_t> expected_replacement;
    if (replacement_valid)
      expected_replacement = this->framebuffer_;
    replacement_valid =
        replacement_valid &&
        this->render_frame_(0, "text", &preceding, 0, true);
    pixoo::FrameView base_refresh_frame;
    replacement_valid =
        replacement_valid && this->content_controller_->RenderContent(
                                 16, "clock_binary", &replacement_overlay, 16,
                                 true, false, true, false,
                                 &base_refresh_frame) &&
        base_refresh_frame.valid();
    pixoo::FrameView replacement_frame;
    replacement_valid =
        replacement_valid && this->content_controller_->RenderContent(
                                 33, "clock_binary", &replacement_overlay, 33,
                                 true, false, false, true,
                                 &replacement_frame) &&
        replacement_frame.valid() &&
        replacement_frame.size == this->framebuffer_.size() &&
        std::equal(replacement_frame.data,
                   replacement_frame.data + replacement_frame.size,
                   expected_replacement.begin());
    if (!replacement_valid) {
      std::printf("render test: FAILED notification-only replacement\n");
      ++failures;
    }

    const pixoo::Notification scrolling{
        "A long message that will not fit", pixoo::Severity::kInfo};
    if (!this->render_frame_(0, "text", &scrolling, 1500, true)) {
      std::printf("render test: FAILED to render notify_scroll\n");
      ++failures;
    } else {
      check("notify_scroll");
    }

    const pixoo::FrameView boot = this->content_controller_->RenderBootAnimation(0);
    const auto pixel_is_black = [](const uint8_t *data, int x, int y) {
      const size_t offset = static_cast<size_t>((y * 64 + x) * 3);
      return data[offset] == 0 && data[offset + 1] == 0 &&
             data[offset + 2] == 0;
    };
    bool boot_valid = boot.valid() && boot.size == this->framebuffer_.size();
    if (boot_valid) {
      std::memcpy(this->framebuffer_.data(), boot.data, boot.size);
      for (int coordinate = 0; coordinate < 64; ++coordinate) {
        boot_valid &= !pixel_is_black(boot.data, coordinate, 0);
        boot_valid &= !pixel_is_black(boot.data, coordinate, 63);
        boot_valid &= !pixel_is_black(boot.data, 0, coordinate);
        boot_valid &= !pixel_is_black(boot.data, 63, coordinate);
      }
      // Dark core at panel center stays unlit before the wordmark fades in.
      boot_valid &= pixel_is_black(boot.data, 32, 32);
      const pixoo::FrameView moved =
          this->content_controller_->RenderBootAnimation(330);
      boot_valid &= moved.valid() && moved.size == this->framebuffer_.size() &&
                    !std::equal(moved.data, moved.data + moved.size,
                                this->framebuffer_.begin());
    }
    if (!boot_valid) {
      std::printf("render test: FAILED to render boot animation\n");
      ++failures;
    }

    const pixoo::FrameView firmware_update =
        this->content_controller_->RenderFirmwareUpdate();
    if (!firmware_update.valid() ||
        firmware_update.size != this->framebuffer_.size()) {
      std::printf("render test: FAILED to render firmware update\n");
      ++failures;
    } else {
      std::memcpy(this->framebuffer_.data(), firmware_update.data,
                  firmware_update.size);
      check("firmware_update");
    }

    const pixoo::Notification off_panel{"Off-panel", pixoo::Severity::kInfo};
    if (!this->render_frame_(0, "text", &off_panel, 0, false) ||
        !std::all_of(this->framebuffer_.begin() +
                         64 * pixoo64::content::NotificationRenderer::kHeight * 3,
                     this->framebuffer_.end(),
                     [](uint8_t value) { return value == 0; })) {
      std::printf("render test: FAILED to render black notification base\n");
      ++failures;
    }

    std::exit(failures == 0 ? 0 : 1);
  });
}

bool RenderTestDisplay::render_frame_(
    uint32_t now_ms, const std::string &dashboard_id,
    const pixoo::Notification *notification,
    uint32_t notification_visible_elapsed_ms, bool base_visible) {
  if (this->content_controller_ == nullptr)
    return false;
  StaticWeatherSource::SetCurrentRenderTime(now_ms);
  pixoo::Overlay overlay;
  const pixoo::Overlay *overlay_ptr = nullptr;
  if (notification != nullptr) {
    overlay.tag = pixoo::OverlayTag::kNotification;
    overlay.notification = *notification;
    overlay_ptr = &overlay;
  }
  pixoo::FrameView frame;
  if (!this->content_controller_->RenderContent(
          now_ms, dashboard_id, overlay_ptr,
          notification_visible_elapsed_ms, base_visible, false, true,
          notification != nullptr, &frame) ||
      !frame.valid() || frame.size != this->framebuffer_.size())
    return false;
  std::memcpy(this->framebuffer_.data(), frame.data, frame.size);
  return true;
}

void RenderTestDisplay::set_content_controller(
    pixoo64::content::ContentController *controller) {
  this->content_controller_ = controller;
}

void RenderTestDisplay::set_output_dir(std::string dir) {
  this->output_dir_ = std::move(dir);
}

void RenderTestDisplay::add_animation_frame(std::string dashboard_id,
                                            uint32_t now_ms,
                                            std::string snapshot_id,
                                            bool base_visible) {
  this->animation_frames_.push_back(AnimationFrame{
      std::move(dashboard_id), now_ms, std::move(snapshot_id), base_visible});
}

bool RenderTestDisplay::has_animation_frames_(
    const std::string &dashboard_id) const {
  for (const AnimationFrame &frame : this->animation_frames_) {
    if (frame.dashboard_id == dashboard_id)
      return true;
  }
  return false;
}

void RenderTestDisplay::draw_pixel_at(int x, int y, Color color) {
  if (x < 0 || x >= 64 || y < 0 || y >= 64)
    return;
  if (!this->clip(x, y))
    return;
  const size_t index = static_cast<size_t>((y * 64 + x) * 3);
  this->framebuffer_[index] = color.r;
  this->framebuffer_[index + 1] = color.g;
  this->framebuffer_[index + 2] = color.b;
}

}  // namespace esphome::pixoo64_render_test
