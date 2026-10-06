#include "render_test_display.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <numeric>
#include <utility>
#include <cerrno>
#include <fcntl.h>
#include <unistd.h>

#include "dashboard/ddp_dashboard.h"
#include "dashboard/now_playing/now_playing_dashboard.h"
#include "dashboard/weather/weather_icon.h"
#include "esphome/components/pixoo64_content/blend_canvas.h"
#include "esphome/core/wake.h"
#include "png.h"

#ifdef USE_PIXOO64_NOW_PLAYING
#include "now_playing_adapter_test.h"
#endif

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
    std::map<std::string, std::vector<uint8_t>> now_playing_frames;
    auto check = [&](const std::string &id) {
      if (id.rfind("now_playing_", 0) == 0)
        now_playing_frames[id] = this->framebuffer_;
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

    if (this->animation_only_) {
      for (const AnimationFrame &frame : this->animation_frames_) {
        if (!this->render_frame_(frame.now_ms, frame.dashboard_id, nullptr, 0,
                                 frame.base_visible, frame.stopwatch,
                                 frame.timer)) {
          std::printf("render test: FAILED to render %s at %ums\n",
                      frame.dashboard_id.c_str(), frame.now_ms);
          ++failures;
        } else if (!frame.snapshot_id.empty()) {
          check(frame.snapshot_id);
        }
      }
      std::exit(failures == 0 ? 0 : 1);
    }

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
                               frame.base_visible, frame.stopwatch,
                               frame.timer)) {
        std::printf("render test: FAILED to render %s at %ums\n",
                    frame.dashboard_id.c_str(), frame.now_ms);
        ++failures;
      } else if (!frame.snapshot_id.empty()) {
        check(frame.snapshot_id);
      }
    }

    const char *now_playing_snapshots[] = {
        "now_playing_playing_artwork",
        "now_playing_showcase",
        "now_playing_paused_midpoint",
        "now_playing_paused",
        "now_playing_buffering",
        "now_playing_track_change_old",
        "now_playing_track_change_pending",
        "now_playing_track_change_ready_start",
        "now_playing_track_change_midpoint",
        "now_playing_track_change_fade_in",
        "now_playing_track_change_ready",
        "now_playing_title_marquee_start",
        "now_playing_title_marquee_scrolled",
        "now_playing_text_only_start",
        "now_playing_text_only_midpoint",
        "now_playing_text_only_fade_in",
        "now_playing_text_only_complete",
        "now_playing_artist_marquee_start",
        "now_playing_artist_marquee_scrolled",
        "now_playing_revision_crossfade_midpoint",
        "now_playing_revision_crossfade_complete",
        "now_playing_duplicate_pending",
        "now_playing_duplicate_ready",
        "now_playing_stable_id_change_pending",
        "now_playing_stable_id_change_ready",
        "now_playing_idle",
        "now_playing_waiting_start",
        "now_playing_waiting_animated",
        "now_playing_unconfigured",
        "now_playing_no_entity_data",
        "now_playing_offline",
        "now_playing_missing_art",
        "now_playing_stale",
        "now_playing_failed_art",
        "now_playing_unsupported_fallback",
        "now_playing_interrupted_old",
        "now_playing_interrupted_pending",
        "now_playing_interrupted_failed_midpoint",
        "now_playing_interrupted_idle_start",
        "now_playing_interrupted_ready_start",
        "now_playing_interrupted_ready_complete",
        "now_playing_fallback_change_pending",
        "now_playing_fallback_change_ready_start",
        "now_playing_fallback_change_midpoint",
        "now_playing_fallback_change_ready",
    };
    bool now_playing_valid = true;
    for (const char *name : now_playing_snapshots) {
      const auto found = now_playing_frames.find(name);
      now_playing_valid &=
          found != now_playing_frames.end() &&
          !std::all_of(found->second.begin(), found->second.end(),
                       [](uint8_t value) { return value == 0; });
    }
    const auto frames_differ = [&](const char *left, const char *right) {
      const auto a = now_playing_frames.find(left);
      const auto b = now_playing_frames.find(right);
      return a != now_playing_frames.end() && b != now_playing_frames.end() &&
             a->second != b->second;
    };
    const auto frame_rows_equal = [&](const char *left, const char *right,
                                      size_t first_row, size_t row_count) {
      const auto a = now_playing_frames.find(left);
      const auto b = now_playing_frames.find(right);
      if (a == now_playing_frames.end() || b == now_playing_frames.end())
        return false;
      constexpr size_t kRowBytes = 64 * 3;
      const size_t first = first_row * kRowBytes;
      const size_t last = first + row_count * kRowBytes;
      return std::equal(a->second.begin() + first, a->second.begin() + last,
                        b->second.begin() + first);
    };
    const auto artwork_pixels_equal = [&](const char *left, const char *right) {
      return frame_rows_equal(left, right, 0, 39);
    };
    const auto cover_pixels_equal = [&](const char *left, const char *right) {
      return frame_rows_equal(left, right, 15, 24);
    };
    now_playing_valid &= frames_differ("now_playing_playing_artwork",
                                       "now_playing_showcase");
    now_playing_valid &= frames_differ("now_playing_playing_artwork",
                                       "now_playing_paused_midpoint");
    now_playing_valid &= frames_differ("now_playing_paused_midpoint",
                                       "now_playing_paused");
    now_playing_valid &= frames_differ("now_playing_paused",
                                       "now_playing_buffering");
    // Pending media keeps the old cover and title. The retained artist may keep
    // scrolling. A ready replacement begins at that same image, crosses through
    // a distinct blend, then reaches the replacement image rather than cutting
    // to it.
    now_playing_valid &= cover_pixels_equal(
        "now_playing_track_change_old", "now_playing_track_change_pending");
    now_playing_valid &= frame_rows_equal(
        "now_playing_track_change_old", "now_playing_track_change_pending", 45, 8);
    now_playing_valid &= cover_pixels_equal(
        "now_playing_track_change_pending",
        "now_playing_track_change_ready_start");
    now_playing_valid &= frame_rows_equal(
        "now_playing_track_change_pending",
        "now_playing_track_change_ready_start", 45, 8);
    now_playing_valid &= !cover_pixels_equal(
        "now_playing_track_change_pending", "now_playing_track_change_midpoint");
    now_playing_valid &= !cover_pixels_equal(
        "now_playing_track_change_midpoint", "now_playing_track_change_fade_in");
    now_playing_valid &= !cover_pixels_equal(
        "now_playing_track_change_fade_in", "now_playing_track_change_ready");
    now_playing_valid &= !frame_rows_equal(
        "now_playing_track_change_midpoint", "now_playing_track_change_fade_in", 45, 18);
    now_playing_valid &= !frame_rows_equal(
        "now_playing_track_change_fade_in", "now_playing_track_change_ready", 45, 18);
    now_playing_valid &= frames_differ("now_playing_title_marquee_start",
                                       "now_playing_title_marquee_scrolled");
    // Metadata-only changes retain the cover while their rows fade through a
    // distinct midpoint and a partially visible replacement.
    now_playing_valid &= artwork_pixels_equal("now_playing_text_only_start",
                                               "now_playing_text_only_midpoint");
    now_playing_valid &= artwork_pixels_equal("now_playing_text_only_midpoint",
                                               "now_playing_text_only_fade_in");
    now_playing_valid &= artwork_pixels_equal("now_playing_text_only_fade_in",
                                               "now_playing_text_only_complete");
    now_playing_valid &= !frame_rows_equal("now_playing_text_only_start",
                                            "now_playing_text_only_midpoint", 45, 18);
    now_playing_valid &= !frame_rows_equal("now_playing_text_only_midpoint",
                                            "now_playing_text_only_fade_in", 45, 18);
    now_playing_valid &= !frame_rows_equal("now_playing_text_only_fade_in",
                                            "now_playing_text_only_complete", 45, 18);
    now_playing_valid &= !frame_rows_equal("now_playing_artist_marquee_start",
                                            "now_playing_artist_marquee_scrolled", 53, 8);
    now_playing_valid &= !artwork_pixels_equal(
        "now_playing_revision_crossfade_midpoint",
        "now_playing_revision_crossfade_complete");
    now_playing_valid &= artwork_pixels_equal(
        "now_playing_revision_crossfade_complete",
        "now_playing_duplicate_pending");
    now_playing_valid &= artwork_pixels_equal("now_playing_duplicate_pending",
                                               "now_playing_duplicate_ready");
    now_playing_valid &= !frame_rows_equal("now_playing_duplicate_pending",
                                            "now_playing_duplicate_ready", 45, 18);
    now_playing_valid &= artwork_pixels_equal(
        "now_playing_duplicate_ready", "now_playing_stable_id_change_pending");
    now_playing_valid &= frame_rows_equal(
        "now_playing_duplicate_ready", "now_playing_stable_id_change_pending", 45, 18);
    now_playing_valid &= !artwork_pixels_equal(
        "now_playing_stable_id_change_pending",
        "now_playing_stable_id_change_ready");
    now_playing_valid &= !frame_rows_equal(
        "now_playing_stable_id_change_pending",
        "now_playing_stable_id_change_ready", 45, 18);
    now_playing_valid &= artwork_pixels_equal(
        "now_playing_fallback_change_pending",
        "now_playing_fallback_change_ready_start");
    now_playing_valid &= !artwork_pixels_equal(
        "now_playing_fallback_change_ready_start",
        "now_playing_fallback_change_midpoint");
    now_playing_valid &= !artwork_pixels_equal(
        "now_playing_fallback_change_midpoint",
        "now_playing_fallback_change_ready");
    now_playing_valid &= !frame_rows_equal(
        "now_playing_fallback_change_pending",
        "now_playing_fallback_change_ready", 45, 18);
    now_playing_valid &= !artwork_pixels_equal(
        "now_playing_fallback_change_pending",
        "now_playing_fallback_change_ready");
    now_playing_valid &= artwork_pixels_equal(
        "now_playing_interrupted_old", "now_playing_interrupted_pending");
    now_playing_valid &= frame_rows_equal(
        "now_playing_interrupted_old", "now_playing_interrupted_pending", 45, 18);
    now_playing_valid &= !artwork_pixels_equal(
        "now_playing_interrupted_old", "now_playing_interrupted_failed_midpoint");
    now_playing_valid &= artwork_pixels_equal(
        "now_playing_interrupted_old", "now_playing_interrupted_idle_start");
    now_playing_valid &= artwork_pixels_equal(
        "now_playing_interrupted_old", "now_playing_interrupted_ready_start");
    now_playing_valid &= !artwork_pixels_equal(
        "now_playing_interrupted_ready_start",
        "now_playing_interrupted_ready_complete");
    now_playing_valid &= frames_differ("now_playing_waiting_start",
                                       "now_playing_waiting_animated");
    if (!now_playing_valid) {
      std::printf("render test: FAILED now-playing visual coverage\n");
      ++failures;
    }

    if (this->text_ == nullptr) {
      std::printf("render test: FAILED text dashboard fixture\n");
      ++failures;
    } else {
      auto render_dashboard_text = [&](const std::string &value, uint32_t now_ms,
                                       const char *dashboard_id) {
        this->text_->publish_state(value);
        return this->render_frame_(now_ms, dashboard_id, nullptr, 0, true);
      };
      auto render_text = [&](const std::string &value, uint32_t now_ms) {
        return render_dashboard_text(value, now_ms, "text");
      };
      if (!render_text("Hello", 0)) {
        std::printf("render test: FAILED short text\n");
        ++failures;
      } else {
        check("text_short");
      }
      if (!render_text("A compact dashboard now wraps ordinary prose into "
                       "readable lines.",
                       100)) {
        std::printf("render test: FAILED wrapped text\n");
        ++failures;
      } else {
        check("text_wrapped");
      }
      if (!render_text("First line\nSecond line", 200)) {
        std::printf("render test: FAILED newline text\n");
        ++failures;
      } else {
        check("text_newline");
      }
      const std::string long_word(128, 'W');
      bool scrolling_valid = render_text(long_word, 1200);
      std::vector<uint8_t> scroll_start;
      if (scrolling_valid)
        scroll_start = this->framebuffer_;
      scrolling_valid = scrolling_valid && render_text(long_word, 2200);
      if (!scrolling_valid || this->framebuffer_ == scroll_start ||
          std::all_of(this->framebuffer_.begin(), this->framebuffer_.end(),
                      [](uint8_t value) { return value == 0; })) {
        std::printf("render test: FAILED scrolling text\n");
        ++failures;
      } else {
        check("text_wide_scroll");
      }
      scrolling_valid = scrolling_valid && render_text(long_word, 4200);
      if (!scrolling_valid || this->framebuffer_ == scroll_start ||
          std::all_of(this->framebuffer_.begin(), this->framebuffer_.end(),
                      [](uint8_t value) { return value == 0; })) {
        std::printf("render test: FAILED scrolling text after three seconds\n");
        ++failures;
      }

      const char *pages = "One\nTwo\nThree\nFour\nFive\nSix\nSeven";
      bool pages_valid = render_text(pages, 2000);
      std::vector<uint8_t> first_page;
      if (pages_valid) {
        first_page = this->framebuffer_;
        check("text_page_1");
      }
      pages_valid = pages_valid && render_text(pages, 5000);
      if (pages_valid) {
        if (this->framebuffer_ == first_page) {
          std::printf("render test: FAILED text page advance\n");
          ++failures;
        }
        check("text_page_2");
      }
      pages_valid = pages_valid && render_text(pages, 8000) &&
                    this->framebuffer_ == first_page;
      pages_valid =
          pages_valid &&
          this->render_frame_(8001, "clock_binary", nullptr, 0, true) &&
          render_text(pages, 8002) && this->framebuffer_ == first_page;
      const char *changed_pages = "One\nTwo\nThree\nFour\nFive\nSix\nChanged";
      pages_valid = pages_valid && render_text(pages, 11002) &&
                    render_text(changed_pages, 11003) &&
                    this->framebuffer_ == first_page;
      if (!pages_valid) {
        std::printf("render test: FAILED text page reset\n");
        ++failures;
      }

      bool input_valid = render_text("", 9000) &&
                         std::all_of(this->framebuffer_.begin(),
                                     this->framebuffer_.end(),
                                     [](uint8_t value) { return value == 0; });
      input_valid = input_valid && render_text("First\r\nSecond\rThird", 9100);
      const std::vector<uint8_t> normalized_lines = this->framebuffer_;
      input_valid = input_valid && render_text("First\nSecond\nThird", 9101) &&
                    this->framebuffer_ == normalized_lines;
      const std::vector<uint8_t> explicit_newlines = this->framebuffer_;
      input_valid = input_valid &&
                    render_text("First\\nSecond\\nThird", 9102) &&
                    this->framebuffer_ == explicit_newlines;
      input_valid = input_valid &&
                    render_text(std::string("A\0B", 3), 9200);
      const std::vector<uint8_t> embedded_nul = this->framebuffer_;
      input_valid = input_valid && render_text("A?B", 9201) &&
                    this->framebuffer_ == embedded_nul;
      input_valid = input_valid &&
                    render_text(std::string("A\xF0\x28\x8C\x28" "B", 6), 9300);
      const std::vector<uint8_t> malformed_utf8 = this->framebuffer_;
      input_valid = input_valid && render_text("A?(?(B", 9301) &&
                    this->framebuffer_ == malformed_utf8;
      const std::string truncated_multibyte =
          std::string(127, 'a') + "\xC3\xA9";
      input_valid = input_valid && render_text(truncated_multibyte, 9400);
      const std::vector<uint8_t> truncated_frame = this->framebuffer_;
      input_valid = input_valid && render_text(std::string(127, 'a'), 9400) &&
                    this->framebuffer_ == truncated_frame;
      if (!input_valid) {
        std::printf("render test: FAILED text input normalization\n");
        ++failures;
      }

      const char *large_pages = "One\nTwo\nThree\nFour\nFive";
      bool large_text_valid =
          render_dashboard_text(large_pages, 10000, "text_16");
      std::vector<uint8_t> large_first_page;
      if (large_text_valid) {
        large_first_page = this->framebuffer_;
        check("text_16_page_1");
      }
      large_text_valid = large_text_valid &&
                         render_dashboard_text(large_pages, 13000, "text_16");
      if (!large_text_valid || this->framebuffer_ == large_first_page) {
        std::printf("render test: FAILED font metrics text layout\n");
        ++failures;
      } else {
        check("text_16_page_2");
      }

      // Restore the fixture used by the established dashboard and notification
      // snapshots below.
      this->text_->publish_state("Hello");
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

    // The normal analog face enters through its wind-in before the banner is
    // captured, rather than being drawn directly in its settled state.
    const pixoo::Notification analog_warning{"Door open",
                                              pixoo::Severity::kWarning};
    if (!this->render_frame_(0, "clock_analog", nullptr, 0, true) ||
        !this->render_frame_(1760, "clock_analog", &analog_warning, 0, true)) {
      std::printf("render test: FAILED settled analog warning\n");
      ++failures;
    } else {
      check("notify_warning_analog");
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
              elapsed_ms, "clock_binary", pixoo::StopwatchSnapshot{},
              pixoo::TimerSnapshot{}, &overlay, elapsed_ms, true, true, false,
              true, &frame) ||
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

    // A reaction captures this recognizable weather dashboard once, then
    // renders from its frozen, blurred, darkened background at a nonzero point.
    bool weather_reaction_valid =
        this->render_frame_(2048, "weather_landscape_day", nullptr, 0, true);
    const std::vector<uint8_t> clean_weather = this->framebuffer_;
    pixoo::Overlay weather_reaction{};
    weather_reaction.tag = pixoo::OverlayTag::kReaction;
    weather_reaction.reaction = pixoo::Reaction::kCelebrate;
    pixoo::FrameView weather_reaction_frame;
    weather_reaction_valid =
        weather_reaction_valid && this->content_controller_->RenderContent(
                                      2048, "weather_landscape_day", {}, {},
                                      &weather_reaction, 0, true, true, false,
                                      true, &weather_reaction_frame) &&
        weather_reaction_frame.valid() &&
        weather_reaction_frame.size == this->framebuffer_.size();
    std::vector<uint8_t> dark_weather;
    if (weather_reaction_valid) {
      dark_weather.assign(weather_reaction_frame.data,
                          weather_reaction_frame.data + weather_reaction_frame.size);
      const uint64_t clean_sum =
          std::accumulate(clean_weather.begin(), clean_weather.end(), uint64_t{0});
      const uint64_t dark_sum =
          std::accumulate(dark_weather.begin(), dark_weather.end(), uint64_t{0});
      weather_reaction_valid &= dark_weather != clean_weather && dark_sum < clean_sum;
    }
    const uint32_t weather_reaction_elapsed =
        pixoo::ReactionVisibleDurationMs(weather_reaction.reaction) * 7 / 20;
    weather_reaction_valid =
        weather_reaction_valid && this->content_controller_->RenderContent(
                                      2048, "weather_landscape_day", {}, {},
                                      &weather_reaction,
                                      weather_reaction_elapsed, true, true,
                                      false, true, &weather_reaction_frame) &&
        weather_reaction_frame.valid() &&
        weather_reaction_frame.size == this->framebuffer_.size();
    if (!weather_reaction_valid) {
      std::printf("render test: FAILED celebration weather reaction\n");
      ++failures;
    } else {
      std::memcpy(this->framebuffer_.data(), weather_reaction_frame.data,
                  weather_reaction_frame.size);
      if (this->framebuffer_ == dark_weather) {
        std::printf("render test: FAILED celebration reaction artwork\n");
        ++failures;
      } else {
        check("reaction_celebrate_weather");
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
        render_reaction(pixoo::Reaction::kLove, 0, true) &&
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
        replacement_valid &&
        this->content_controller_->RenderContent(
            16, "clock_binary", pixoo::StopwatchSnapshot{},
            pixoo::TimerSnapshot{}, &replacement_overlay, 16, true, false, true,
            false, &base_refresh_frame) &&
        base_refresh_frame.valid();
    pixoo::FrameView replacement_frame;
    replacement_valid =
        replacement_valid &&
        this->content_controller_->RenderContent(
            33, "clock_binary", pixoo::StopwatchSnapshot{},
            pixoo::TimerSnapshot{}, &replacement_overlay, 33, true, false,
            false, true, &replacement_frame) &&
        replacement_frame.valid() &&
        replacement_frame.size == this->framebuffer_.size() &&
        std::equal(replacement_frame.data,
                   replacement_frame.data + replacement_frame.size,
                   expected_replacement.begin());
    if (!replacement_valid) {
      std::printf("render test: FAILED notification-only replacement\n");
      ++failures;
    }

    const pixoo::Notification titled{
        "Message", pixoo::Severity::kSuccess, "Status"};
    if (!this->render_frame_(0, "text", &titled, 0, true)) {
      std::printf("render test: FAILED to render notify_title\n");
      ++failures;
    } else {
      check("notify_title");
    }

    const pixoo::Notification scrolling{
        "A long message that will not fit", pixoo::Severity::kInfo};
    if (!this->render_frame_(0, "text", &scrolling, 1500, true)) {
      std::printf("render test: FAILED to render notify_scroll\n");
      ++failures;
    } else {
      check("notify_scroll");
    }

    const pixoo::Notification titled_scrolling{
        "Message", pixoo::Severity::kInfo,
        "A long title that will not fit"};
    const pixoo::Notification title_only{
        "", pixoo::Severity::kInfo, titled_scrolling.title};
    const pixoo::Notification message_only{
        titled_scrolling.text, pixoo::Severity::kInfo};
    const uint32_t titled_scroll_pass =
        this->content_controller_->NotificationMinVisibleMs(titled_scrolling);
    const uint32_t title_scroll_pass =
        this->content_controller_->NotificationMinVisibleMs(title_only);
    const uint32_t message_scroll_pass =
        this->content_controller_->NotificationMinVisibleMs(message_only);
    if (titled_scroll_pass != title_scroll_pass ||
        titled_scroll_pass <= message_scroll_pass) {
      std::printf("render test: FAILED notification scroll duration\n");
      ++failures;
    }
    if (!this->render_frame_(0, "text", &titled_scrolling, 1500, true)) {
      std::printf("render test: FAILED to render notify_title_scroll\n");
      ++failures;
    } else {
      check("notify_title_scroll");
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

    // Visibility hooks are a lifecycle contract rather than a pixel snapshot.
    // Exercise replacement, repeated hidden frames, and explicit clearing with
    // the same deterministic tick passed to RenderContent().
    struct LifecycleDashboard final : pixoo64::dashboard::Dashboard {
      bool available() const override { return true; }
      void Render(display::Display &) const override {}
      void OnShow(uint32_t now_ms) override {
        ++show_count;
        last_show_ms = now_ms;
      }
      void OnHide(uint32_t now_ms) override {
        ++hide_count;
        last_hide_ms = now_ms;
      }
      int show_count{0};
      int hide_count{0};
      uint32_t last_show_ms{0};
      uint32_t last_hide_ms{0};
    } first, second;
    first.set_id("__lifecycle_first");
    second.set_id("__lifecycle_second");
    this->content_controller_->HideBaseContent(999);
    this->content_controller_->add_dashboard(&first);
    this->content_controller_->add_dashboard(&second);
    const auto render_lifecycle = [this](uint32_t now_ms, const char *id,
                                         bool base_visible) {
      pixoo::FrameView frame;
      return this->content_controller_->RenderContent(
          now_ms, id, {}, {}, nullptr, 0, base_visible, false, true, false,
          &frame);
    };
    bool lifecycle_valid =
        render_lifecycle(1000, "__lifecycle_first", true) &&
        render_lifecycle(1001, "__lifecycle_first", true) &&
        first.show_count == 1 && first.hide_count == 0 &&
        render_lifecycle(1002, "__lifecycle_second", true) &&
        first.hide_count == 1 && first.last_hide_ms == 1002 &&
        second.show_count == 1 && second.last_show_ms == 1002 &&
        render_lifecycle(1003, "__lifecycle_second", false) &&
        render_lifecycle(1004, "__lifecycle_second", false) &&
        second.hide_count == 1 && second.last_hide_ms == 1003 &&
        render_lifecycle(1005, "__lifecycle_first", true) &&
        first.show_count == 2 && first.last_show_ms == 1005;
    this->content_controller_->HideBaseContent(1006);
    this->content_controller_->HideBaseContent(1007);
    lifecycle_valid &= first.hide_count == 2 && first.last_hide_ms == 1006;
    if (!lifecycle_valid) {
      std::printf("render test: FAILED dashboard visibility lifecycle\n");
      ++failures;
    }

    uint32_t ddp_clock_ms = 2000;
    struct TestDdpDashboard final : pixoo64::dashboard::DdpDashboard {
      explicit TestDdpDashboard(uint32_t &clock_ms) : clock_ms_(clock_ms) {}
      uint32_t now() const { return this->clock_ms_; }
      uint32_t advance(uint32_t elapsed_ms = 1) {
        return this->clock_ms_ += elapsed_ms;
      }
      void OnShow(uint32_t now_ms) override {
        this->clock_ms_ = std::max(this->clock_ms_, now_ms);
        DdpDashboard::OnShow(this->clock_ms_);
      }
      void OnHide(uint32_t now_ms) override {
        this->clock_ms_ = std::max(this->clock_ms_, now_ms);
        DdpDashboard::OnHide(this->clock_ms_);
      }
      void loop() override { this->Service_(this->clock_ms_); }
      void on_shutdown() override { this->Stop_(this->clock_ms_); }
      int fd() const { return this->listener_ ? this->listener_->get_fd() : -1; }
      void close_listener() {
        if (this->listener_ != nullptr)
          this->listener_->close();
      }
     private:
      uint32_t &clock_ms_;
    } ddp(ddp_clock_ms);
    ddp.set_id("__ddp");
    bool ddp_valid = ddp.available() && ddp.ReadyToShow() &&
                     ddp.HasPresentation() && !ddp.active() &&
                     !ddp.requires_microphone() && ddp.frame_interval_ms() == 33;
    ddp.Prepare(ddp.now());
    ddp.Prepare(ddp.advance());
    ddp_valid &= !ddp.active() && ddp.ReadyToShow();
    ddp.CancelPreparation(ddp.advance());
    ddp_valid &= !ddp.active();
    ddp.OnShow(ddp.advance());
    ddp_valid &= ddp.active();
    ddp.OnHide(ddp.advance());
    ddp_valid &= !ddp.active() && ddp.HasPresentation();

    // Clear a colored display through the dashboard's ordinary drawing path.
    std::fill(this->framebuffer_.begin(), this->framebuffer_.end(), 255);
    ddp.Render(*this);
    ddp_valid &= std::all_of(this->framebuffer_.begin(),
                            this->framebuffer_.end(),
                            [](uint8_t value) { return value == 0; });
    this->content_controller_->add_dashboard(&ddp);
    pixoo::DashboardSelection ddp_selection;
    ddp_valid &= this->content_controller_->ResolveDashboard("__ddp", &ddp_selection) &&
                 ddp_selection.frame_interval_ms == 33;
    ddp_valid &= this->render_frame_(ddp.advance(), "__ddp", nullptr, 0, true) &&
                 ddp.active() &&
                 std::all_of(this->framebuffer_.begin(),
                             this->framebuffer_.end(),
                             [](uint8_t value) { return value == 0; });
    ddp_valid &= this->render_frame_(ddp.advance(), "__ddp", nullptr, 0, true) && ddp.active();
    ddp_valid &= render_lifecycle(ddp.advance(), "__lifecycle_first", true) && !ddp.active();
    ddp_valid &= render_lifecycle(ddp.advance(), "__ddp", true) && ddp.active();
    ddp.loop();
    ddp_valid &= ddp.fd() >= 0 && (::fcntl(ddp.fd(), F_GETFL) & O_NONBLOCK) != 0;
    const int sender = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (ddp.fd() >= 0 && sender >= 0) {
      sockaddr_in destination{};
      destination.sin_family = AF_INET;
      destination.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
      destination.sin_port = htons(4048);
      const auto send_packet = [&](const uint8_t *data, size_t size) {
        const bool sent = ::sendto(sender, data, size, 0,
                                   reinterpret_cast<sockaddr *>(&destination), sizeof(destination)) ==
                          static_cast<ssize_t>(size);
        // Host loopback delivery may finish on another thread after sendto returns.
        ::usleep(1000);
        return sent;
      };
      const auto packet = [](size_t offset, const uint8_t *data, size_t size, bool push) {
        std::vector<uint8_t> result(10 + size);
        result[0] = push ? 0x41 : 0x40;
        result[1] = 7;  // LedFX uses the same sequence across a frame's chunks.
        result[2] = 0x0B;
        result[3] = 1;
        result[4] = offset >> 24;
        result[5] = offset >> 16;
        result[6] = offset >> 8;
        result[7] = offset;
        result[8] = size >> 8;
        result[9] = size;
        if (size) std::memcpy(result.data() + 10, data, size);
        return result;
      };
      const auto send_ddp = [&](const std::vector<uint8_t> &data) {
        ddp_valid &= send_packet(data.data(), data.size());
      };
      const auto drain = [&]() {
        // A slow host may hit the time budget before the packet cap.
        for (int i = 0; i < 8; ++i) ddp.loop();
      };
      const auto render_ddp = [&]() {
        ddp_valid &= this->render_frame_(ddp.advance(), "__ddp", nullptr, 0, true);
      };
      std::vector<uint8_t> expected(pixoo::ddp::kFrameBytes);
      for (size_t i = 0; i < expected.size(); ++i) expected[i] = (i * 17 + i / 192) & 255;
      for (size_t offset = 0; offset < expected.size(); offset += 1440) {
        const size_t size = std::min(size_t(1440), expected.size() - offset);
        send_ddp(packet(offset, expected.data() + offset, size, false));
        drain();
      }
      render_ddp();
      ddp_valid &= std::all_of(this->framebuffer_.begin(), this->framebuffer_.end(),
                              [](uint8_t value) { return value == 0; });
      send_ddp(packet(0, nullptr, 0, true));
      drain();
      ddp.OnShow(ddp.advance());  // Repeated entry must retain the published frame.
      render_ddp();
      ddp_valid &= this->framebuffer_ == expected;
      auto diagnostics = ddp.diagnosticsSnapshot(ddp.now());
      ddp_valid &= diagnostics.received == 10 && diagnostics.rejected == 0 &&
                   diagnostics.publications == 1 && diagnostics.rendered_revisions == 1 &&
                   diagnostics.latest_revision == 1 && diagnostics.receive_passes > 0;
      render_ddp();
      ddp_valid &= ddp.diagnosticsSnapshot(ddp.now()).rendered_revisions == 1;

      // Zero-length and truncated oversized UDP datagrams must not stop the drain
      // or publish bytes from an otherwise valid-looking header.
      ddp_valid &= send_packet(nullptr, 0);
      auto oversized = packet(0, expected.data(), 1440, true);
      oversized.resize(4096, 255);
      std::fill(oversized.begin() + 10, oversized.end(), 255);
      send_ddp(oversized);
      drain();
      render_ddp();
      ddp_valid &= this->framebuffer_ == expected && ddp.fd() >= 0;
      diagnostics = ddp.diagnosticsSnapshot(ddp.now());
      ddp_valid &= diagnostics.received == 12 && diagnostics.rejected == 2 &&
                   diagnostics.publications == 1 && diagnostics.rendered_revisions == 1;
      const uint8_t first_rgb[] = {12, 34, 56};
      const uint8_t latest_rgb[] = {78, 90, 123};
      send_ddp(packet(0, first_rgb, 3, true));
      send_ddp(packet(0, latest_rgb, 3, true));
      drain();
      std::copy_n(latest_rgb, 3, expected.begin());
      ddp_valid &= ddp.diagnosticsSnapshot(ddp.now()).latest_revision == 3;
      render_ddp();
      ddp_valid &= ddp.diagnosticsSnapshot(ddp.now()).rendered_revisions == 2;
      ddp_valid &= this->framebuffer_ == expected;

      // At most 32 attempts: 33 small packets fit ordinary host UDP queues. Peek
      // proves an early stop without depending on host speed or ready() state.
      const auto marker = packet(0, latest_rgb, 3, true);
      for (int i = 0; i < 33; ++i) send_ddp(marker);
      wake_request_take();
      ddp.loop();
      ddp_valid &= wake_request_take();
      uint8_t peek;
      ddp_valid &= ddp.fd() >= 0 && ::recv(ddp.fd(), &peek, 1, MSG_PEEK) == 1;
      drain();
      ddp_valid &= ::recv(ddp.fd(), &peek, 1, MSG_PEEK) == -1 &&
                   (errno == EAGAIN || errno == EWOULDBLOCK);
      wake_request_take();
      ddp.loop();
      ddp_valid &= !wake_request_take();  // An empty queue does not rearm service.

      const pixoo::Notification ddp_note{"Streaming", pixoo::Severity::kInfo};
      send_ddp(packet(3, first_rgb, 3, true));
      drain();
      std::copy_n(first_rgb, 3, expected.begin() + 3);
      ddp_valid &= this->render_frame_(ddp.advance(), "__ddp", &ddp_note, 0, true);
      const size_t banner_bytes = 64 * pixoo64::content::NotificationRenderer::kHeight * 3;
      ddp_valid &= std::equal(expected.begin() + banner_bytes, expected.end(),
                              this->framebuffer_.begin() + banner_bytes);
      pixoo::Overlay ddp_reaction;
      ddp_reaction.tag = pixoo::OverlayTag::kReaction;
      ddp_reaction.reaction = pixoo::Reaction::kLaughing;
      render_ddp();  // Reaction entry must capture clean DDP, not the banner.
      ddp_valid &= this->framebuffer_ == expected;
      pixoo::FrameView frozen;
      ddp_valid &= this->content_controller_->RenderContent(
          ddp.advance(), "__ddp", {}, {}, &ddp_reaction, 0, true, true, true, true, &frozen);
      const bool control_rendered = this->content_controller_->RenderContent(
          ddp.advance(), "__ddp", {}, {}, &ddp_reaction, 250, true, true, false, true, &frozen);
      std::vector<uint8_t> frozen_copy;
      if (control_rendered && frozen.data != nullptr &&
          frozen.size == pixoo::ddp::kFrameBytes) {
        frozen_copy.assign(frozen.data, frozen.data + frozen.size);
      } else {
        std::printf("render test: FAILED DDP reaction control frame invalid\n");
        ddp_valid = false;
      }
      const auto before_frozen = ddp.diagnosticsSnapshot(ddp.now());
      send_ddp(packet(6, latest_rgb, 3, true));
      drain();  // No dashboard Tick during a reaction; Component::loop still runs.
      std::copy_n(latest_rgb, 3, expected.begin() + 6);
      const bool updated_rendered = this->content_controller_->RenderContent(
          ddp.advance(), "__ddp", {}, {}, &ddp_reaction, 250, true, true, false, true, &frozen);
      ddp_valid &= updated_rendered && frozen.data != nullptr &&
                   frozen.size == pixoo::ddp::kFrameBytes &&
                   frozen_copy.size() == frozen.size &&
                   std::equal(frozen_copy.begin(), frozen_copy.end(), frozen.data);
      ddp_valid &= ddp.diagnosticsSnapshot(ddp.now()).rendered_revisions ==
                   before_frozen.rendered_revisions;
      ddp_valid &= ddp.diagnosticsSnapshot(ddp.now()).latest_revision ==
                   before_frozen.latest_revision + 1;
      render_ddp();
      ddp_valid &= this->framebuffer_ == expected;
      ddp_valid &= ddp.diagnosticsSnapshot(ddp.now()).rendered_revisions ==
                   before_frozen.rendered_revisions + 1;

      TestDdpDashboard competing(ddp_clock_ms);
      competing.OnShow(ddp.advance());
      competing.loop();
      ddp_valid &= competing.active() && competing.fd() == -1 &&
                   competing.status_has_warning() && !competing.is_failed();
      competing.loop();
      ddp_valid &= competing.fd() == -1;
      competing.on_shutdown();

      // Drop both unpublished assembly bytes and queued input on hide/re-entry.
      send_ddp(packet(0, first_rgb, 3, false));
      drain();
      send_ddp(packet(9, latest_rgb, 3, true));
      this->content_controller_->HideBaseContent(ddp.advance());
      wake_request_take();
      ddp.loop();
      ddp_valid &= !wake_request_take();
      ddp_valid &= !ddp.active() && ddp.fd() == -1 && ddp.ReadyToShow();
      send_ddp(packet(12, latest_rgb, 3, true));  // Hidden port has no listener.
      render_ddp();
      ddp_valid &= ddp.fd() == -1;
      ddp.loop();
      drain();
      render_ddp();
      ddp_valid &= std::all_of(this->framebuffer_.begin(), this->framebuffer_.end(),
                              [](uint8_t value) { return value == 0; });
      send_ddp(packet(0, nullptr, 0, true));
      drain();
      render_ddp();
      ddp_valid &= std::all_of(this->framebuffer_.begin(), this->framebuffer_.end(),
                              [](uint8_t value) { return value == 0; });
      send_ddp(packet(0, latest_rgb, 3, true));
      drain();
      ddp.close_listener();  // Force a runtime receive error without test hooks in production.
      wake_request_take();
      ddp.loop();
      ddp_valid &= !wake_request_take();
      const uint32_t failed_at_ms = ddp.now();
      ddp_valid &= ddp.active() && ddp.fd() == -1 &&
                   ddp.status_has_warning() && !ddp.is_failed();
      ddp.loop();
      ddp_valid &= ddp.fd() == -1;
      render_ddp();
      ddp_valid &= std::equal(latest_rgb, latest_rgb + 3, this->framebuffer_.begin());
      const auto retained_frame = this->framebuffer_;
      ddp.advance(999 - (ddp.now() - failed_at_ms));
      ddp.loop();
      ddp_valid &= ddp.fd() == -1 && ddp.status_has_warning();
      ddp.advance(1);
      ddp.loop();
      const bool reopened = ddp.fd() >= 0 && !ddp.status_has_warning();
      if (!reopened)
        std::printf("render test: FAILED DDP listener did not recover after 1s\n");
      ddp_valid &= reopened;
      diagnostics = ddp.diagnosticsSnapshot(ddp.now());
      ddp_valid &= diagnostics.socket_errors == 1 && diagnostics.elapsed_ms < 5000;
      render_ddp();
      ddp_valid &= ddp.diagnosticsSnapshot(ddp.now()).rendered_revisions ==
                   diagnostics.rendered_revisions;
      ddp_valid &= this->framebuffer_ == retained_frame;
      send_ddp(packet(0, first_rgb, 3, true));
      drain();
      render_ddp();
      auto recovered_frame = retained_frame;
      std::copy_n(first_rgb, 3, recovered_frame.begin());
      ddp_valid &= this->framebuffer_ == recovered_frame;
      ddp.on_shutdown();
      ddp.loop();
      ddp_valid &= !ddp.active() && ddp.fd() == -1 && !ddp.status_has_warning();

      // Exercise the real application policy with the real renderer and UDP
      // receiver. The panel copies borrowed frames before the renderer reuses them.
      struct RecordingPanel final : pixoo::PanelPort {
        explicit RecordingPanel(TestDdpDashboard &dashboard) : ddp(dashboard) {}
        TestDdpDashboard &ddp;
        bool powered{false};
        bool initialize_ok{true};
        bool valid_frames{true};
        bool expect_closed{false};
        bool closed_at_present{false};
        bool forced{false};
        float brightness{0};
        size_t brightness_calls{0};
        size_t presents{0};
        std::vector<uint8_t> pixels;
        void SetPower(bool on) override { powered = on; }
        bool Initialize() override { return powered && initialize_ok; }
        void SetBrightness(float value) override {
          brightness = value;
          ++brightness_calls;
        }
        bool Present(pixoo::FrameView frame, bool force) override {
          ++presents;
          forced = force;
          if (expect_closed) closed_at_present = !ddp.active() && ddp.fd() == -1;
          if (frame.data == nullptr || frame.size != pixoo::ddp::kFrameBytes) {
            valid_frames = false;
            return false;
          }
          pixels.assign(frame.data, frame.data + frame.size);
          return true;
        }
      } panel(ddp);
      bool app_valid = true;
      const auto check_app = [&](bool condition, const char *what) {
        if (!condition) {
          std::printf("render test: FAILED DDP application %s\n", what);
          app_valid = false;
        }
      };
      pixoo::FirmwareAppConfig config;
      config.cold_init_delay_ms = 0;
      config.repower_delay_ms = 0;
      config.boot_animation_ms = 0;
      // Observe update ordering without substituting any rendering behavior.
      struct ObservedRenderer final : pixoo::RenderPort {
        ObservedRenderer(pixoo::RenderPort &renderer, TestDdpDashboard &dashboard)
            : real(renderer), ddp(dashboard) {}
        pixoo::RenderPort &real;
        TestDdpDashboard &ddp;
        bool closed_before_update{false};
        bool ResolveDashboard(const std::string &id,
                              pixoo::DashboardSelection *selection) override {
          return real.ResolveDashboard(id, selection);
        }
        pixoo::FrameView RenderBootAnimation(uint32_t elapsed) override {
          return real.RenderBootAnimation(elapsed);
        }
        pixoo::FrameView RenderFirmwareUpdate() override {
          closed_before_update = !ddp.active() && ddp.fd() == -1;
          return real.RenderFirmwareUpdate();
        }
        uint32_t NotificationMinVisibleMs(const pixoo::Notification &note) override {
          return real.NotificationMinVisibleMs(note);
        }
        void HideBaseContent(uint32_t now) override { real.HideBaseContent(now); }
        void ReleaseOverlayResources() override { real.ReleaseOverlayResources(); }
        bool RenderContent(uint32_t now, const std::string &id,
                           const pixoo::StopwatchSnapshot &stopwatch,
                           const pixoo::TimerSnapshot &timer, const pixoo::Overlay *overlay,
                           uint32_t elapsed, bool visible, bool frozen, bool base,
                           bool composite, pixoo::FrameView *frame) override {
          return real.RenderContent(now, id, stopwatch, timer, overlay, elapsed,
                                    visible, frozen, base, composite, frame);
        }
      } renderer(*this->content_controller_, ddp);
      pixoo::FirmwareApp app(panel, renderer, nullptr, nullptr, nullptr, config);
      const auto tick = [&](uint32_t elapsed_ms = 0) {
        app.Tick(ddp.advance(elapsed_ms));
      };
      const auto black_panel = [&]() {
        return panel.pixels.size() == pixoo::ddp::kFrameBytes &&
               std::all_of(panel.pixels.begin(), panel.pixels.end(),
                           [](uint8_t value) { return value == 0; });
      };
      check_app(app.Start(ddp.advance(), {true, 1.0f}, "__ddp"), "startup");
      check_app(!ddp.active() && ddp.fd() == -1 && panel.presents == 0,
                "does not receive before initialization");
      tick();  // Initialize; zero-duration boot still has its own lifecycle tick.
      check_app(app.phase() == pixoo::FirmwareApp::Phase::kBootAnimation &&
                    !ddp.active(), "boot hides receiver");
      tick();
      check_app(app.phase() == pixoo::FirmwareApp::Phase::kRunning &&
                    ddp.active() && black_panel(), "running enters black");
      ddp.loop();
      const bool app_listener_ready = ddp.fd() >= 0;
      check_app(app_listener_ready, "UDP setup (check port 4048 occupancy)");
      if (app_listener_ready) {
        std::vector<uint8_t> app_expected(pixoo::ddp::kFrameBytes, 0);
        const size_t tail = app_expected.size() - 3;
        const auto push_tail = [&](const uint8_t *rgb) {
          const auto datagram = packet(tail, rgb, 3, true);
          check_app(send_packet(datagram.data(), datagram.size()), "UDP send");
          drain();
          std::copy_n(rgb, 3, app_expected.begin() + tail);
        };
        const auto tail_matches = [&]() {
          return panel.pixels.size() == app_expected.size() &&
                 std::equal(app_expected.begin() + tail, app_expected.end(),
                            panel.pixels.begin() + tail);
        };
        size_t count = panel.presents;
        auto received = ddp.diagnosticsSnapshot(ddp.now());
        push_tail(first_rgb);
        check_app(panel.presents == count && black_panel() &&
                      ddp.diagnosticsSnapshot(ddp.now()).latest_revision ==
                          received.latest_revision + 1,
                  "receive publishes without presenting");
        tick(32);
        check_app(panel.presents == count, "33ms cadence waits");
        tick(1);
        check_app(panel.presents == ++count && panel.pixels == app_expected,
                  "33ms cadence presents published image");
        push_tail(first_rgb);
        push_tail(latest_rgb);
        push_tail(first_rgb);
        push_tail(latest_rgb);
        check_app(panel.presents == count, "rapid PUSH does not present");
        tick(99);  // Three deadlines passed; exactly one latest frame is due.
        check_app(panel.presents == ++count && panel.pixels == app_expected,
                  "missed deadlines present latest once");
        tick();
        tick(32);
        check_app(panel.presents == count, "no queued presentation burst");
        tick(1);
        check_app(panel.presents == ++count && panel.pixels == app_expected,
                  "cadence resumes after missed deadlines");

        const size_t light_calls = panel.brightness_calls;
        app.SetUserLight({true, 0.25f}, ddp.now());
        tick();
        check_app(panel.brightness == 0.25f && panel.brightness_calls > light_calls &&
                      panel.pixels == app_expected, "brightness port leaves payload raw");

        pixoo::NotificationRequest note;
        note.notification = pixoo::Notification{"Live", pixoo::Severity::kInfo};
        note.requested_duration_ms = 100;
        const uint32_t note_duration = std::max(
            note.requested_duration_ms,
            this->content_controller_->NotificationMinVisibleMs(note.notification));
        check_app(app.Notify(note, ddp.now()), "enqueue notification");
        tick();
        check_app(app.notification_visible() && ddp.active() && tail_matches(),
                  "notification keeps base live");
        push_tail(first_rgb);
        tick(33);
        check_app(tail_matches(), "notification refreshes incoming pixels");
        check_app(app.React(pixoo::Reaction::kLaughing, ddp.now()) &&
                      app.Notify(note, ddp.now()), "enqueue reaction then notification");
        tick(note_duration - 33);
        check_app(app.overlay_visible() && app.current_overlay() != nullptr &&
                      app.current_overlay()->tag == pixoo::OverlayTag::kReaction &&
                      ddp.active() && ddp.fd() >= 0, "reaction keeps listener open");
        const auto frozen_metrics = ddp.diagnosticsSnapshot(ddp.now());
        count = panel.presents;
        push_tail(first_rgb);
        push_tail(latest_rgb);
        check_app(panel.presents == count, "frozen reception does not present");
        tick(33);
        const auto live_metrics = ddp.diagnosticsSnapshot(ddp.now());
        check_app(live_metrics.latest_revision == frozen_metrics.latest_revision + 2 &&
                      live_metrics.rendered_revisions == frozen_metrics.rendered_revisions,
                  "reaction receives without rendering base");
        tick(pixoo::ReactionVisibleDurationMs(pixoo::Reaction::kLaughing) - 33);
        check_app(app.notification_visible() && tail_matches() &&
                      ddp.diagnosticsSnapshot(ddp.now()).rendered_revisions ==
                          frozen_metrics.rendered_revisions + 1,
                  "reaction to notification resumes latest image");
        tick(note_duration);
        check_app(app.overlay_queue_size() == 0 && panel.pixels == app_expected,
                  "notification to base has no stale queue");

        app.SelectDashboard("__lifecycle_first");
        tick();
        check_app(!ddp.active() && ddp.fd() == -1, "switch away closes listener");
        app.SelectDashboard("__ddp");
        tick();
        check_app(ddp.active() && black_panel() && ddp.fd() == -1,
                  "reentry resets black before receiver loop");
        ddp.loop();
        check_app(ddp.fd() >= 0, "reentry opens listener");
        push_tail(latest_rgb);
        tick(33);
        check_app(tail_matches(), "reentry receives live pixels");

        app.SetUserLight({false, 0.25f}, ddp.now());
        count = panel.presents;
        check_app(app.phase() == pixoo::FirmwareApp::Phase::kOff && !panel.powered &&
                      !ddp.active() && ddp.fd() == -1, "off closes listener");
        tick(33);
        check_app(panel.presents == count, "off suppresses presentation");
        check_app(app.Notify(note, ddp.now()), "off notification wake");
        tick();
        tick();
        ddp.loop();
        check_app(app.notification_visible() && panel.powered && !ddp.active() &&
                      ddp.fd() == -1, "temporary wake never opens base receiver");
        app.ClearOverlayQueue(ddp.now());
        check_app(app.phase() == pixoo::FirmwareApp::Phase::kOff && !panel.powered,
                  "temporary wake restores off");
        app.SelectDashboard("__lifecycle_first");
        app.SelectDashboard("__ddp");
        tick();
        ddp.loop();
        check_app(!ddp.active() && ddp.fd() == -1, "off selection stays hidden");
        panel.initialize_ok = false;
        app.SetUserLight({true, 0.25f}, ddp.now());
        tick();
        check_app(panel.powered && app.phase() == pixoo::FirmwareApp::Phase::kWaitingInit &&
                      !ddp.active(), "selection waits for powered initialization");
        panel.initialize_ok = true;
        tick();
        check_app(app.phase() == pixoo::FirmwareApp::Phase::kRunning && !ddp.active(),
                  "selection waits for running render");
        tick();
        check_app(ddp.active() && black_panel(), "off selection enters reset black");
        ddp.loop();
        check_app(ddp.fd() >= 0, "repower listener opens");
        push_tail(latest_rgb);
        tick(33);

        panel.expect_closed = true;
        count = panel.presents;
        check_app(app.BeginFirmwareUpdate(ddp.now()) && panel.presents == count + 1 &&
                      panel.forced && panel.closed_at_present && renderer.closed_before_update,
                  "update closes listener before rendering and force-presents");
        const auto update_frame = this->content_controller_->RenderFirmwareUpdate();
        check_app(update_frame.data != nullptr && update_frame.size == panel.pixels.size() &&
                      std::equal(panel.pixels.begin(), panel.pixels.end(), update_frame.data) &&
                      panel.presents == count + 1,
                  "application presents the real update frame");
        check_app(app.phase() == pixoo::FirmwareApp::Phase::kRunning,
                  "update preserves existing lifecycle phase");
        panel.expect_closed = false;
        tick();
        check_app(ddp.active() && black_panel(), "post-update tick rebuilds base");
      }
      check_app(panel.valid_frames, "valid synchronous frame copies");
      app.SetUserLight({false, 0.25f}, ddp.now());
      this->content_controller_->HideBaseContent(ddp.advance());
      if (!app_valid) ++failures;
      else std::printf("render test: ok DDP application lifecycle and scheduling\n");
    } else {
      std::printf("render test: FAILED DDP UDP setup (listener=%d, sender=%d); "
                  "check whether port 4048 is occupied\n", ddp.fd(), sender);
      ddp_valid = false;
    }
    ddp.on_shutdown();
    this->content_controller_->HideBaseContent(ddp.advance());
    if (sender >= 0) ::close(sender);
    if (!ddp_valid) {
      std::printf("render test: FAILED DDP readiness, lifecycle, or UDP rendering\n");
      ++failures;
    } else {
      std::printf("render test: ok DDP readiness, lifecycle, and UDP rendering\n");
    }

    const size_t now_playing_ticks = static_cast<size_t>(std::count_if(
        this->animation_frames_.begin(), this->animation_frames_.end(),
        [](const AnimationFrame &frame) {
          return frame.dashboard_id == "now_playing";
        }));
    const bool now_playing_lifecycle_valid =
        now_playing_ticks == 0
            ? this->now_playing_source_ == nullptr
            : this->now_playing_source_ != nullptr &&
                  this->now_playing_source_->eligible_true_count() == 1 &&
                  this->now_playing_source_->eligible_false_count() == 1 &&
                  this->now_playing_source_->data_count() == now_playing_ticks + 1 &&
                  this->now_playing_source_->copy_count() == 9;
    if (!now_playing_lifecycle_valid) {
      std::printf("render test: FAILED now-playing source lifecycle\n");
      ++failures;
    }

    // "I Love you" occupies exactly the reported 60px width only when the
    // first glyph's separate 2px x offset is omitted. Its full advance is 62px,
    // so it must move to expose the clipped final column.
    bool marquee_boundary_valid =
        this->render_frame_(50000, "now_playing", nullptr, 0, true);
    std::vector<uint8_t> marquee_boundary_start;
    if (marquee_boundary_valid)
      marquee_boundary_start = this->framebuffer_;
    marquee_boundary_valid =
        marquee_boundary_valid &&
        this->render_frame_(50980, "now_playing", nullptr, 0, true);
    if (marquee_boundary_valid) {
      constexpr size_t kTitleStart = 45u * 64u * 3u;
      constexpr size_t kTitleEnd = 53u * 64u * 3u;
      marquee_boundary_valid =
          !std::equal(marquee_boundary_start.begin() + kTitleStart,
                      marquee_boundary_start.begin() + kTitleEnd,
                      this->framebuffer_.begin() + kTitleStart);
    }
    if (!marquee_boundary_valid) {
      std::printf("render test: FAILED now-playing marquee width boundary\n");
      ++failures;
    }
    this->content_controller_->HideBaseContent(50981);

    struct GateDashboard final : pixoo64::dashboard::Dashboard {
      explicit GateDashboard(Color color) : color(color) {}
      bool available() const override { return true; }
      void Prepare(uint32_t) override { ++prepare_count; }
      void CancelPreparation(uint32_t) override { ++cancel_count; }
      bool ReadyToShow() const override { return ready; }
      bool HasPresentation() const override { return has_presentation; }
      void OnShow(uint32_t) override { ++show_count; }
      void OnHide(uint32_t) override { ++hide_count; }
      void Tick(uint32_t) override {
        ++tick_count;
        if (invalidate_on_tick)
          has_presentation = false;
      }
      void Render(display::Display &display) const override {
        display.fill(color);
      }
      Color color;
      bool ready{true};
      bool has_presentation{true};
      bool invalidate_on_tick{false};
      int prepare_count{0};
      int cancel_count{0};
      int show_count{0};
      int hide_count{0};
      int tick_count{0};
    } outgoing(Color(18, 42, 96)), waiting(Color(31, 112, 68)),
        pending_replacement(Color(88, 36, 116)), cold(Color(72, 72, 72));
    outgoing.set_id("__prepare_outgoing");
    waiting.set_id("__prepare_waiting");
    pending_replacement.set_id("__prepare_replacement");
    cold.set_id("__prepare_cold");
    waiting.ready = false;
    pending_replacement.ready = false;
    cold.ready = false;
    this->content_controller_->add_dashboard(&outgoing);
    this->content_controller_->add_dashboard(&waiting);
    this->content_controller_->add_dashboard(&pending_replacement);
    this->content_controller_->add_dashboard(&cold);
    const auto render_preparation = [this](uint32_t now_ms, const char *id) {
      StaticWeatherSource::SetCurrentRenderTime(now_ms);
      StaticNowPlayingSource::SetCurrentRenderTime(now_ms);
      pixoo::FrameView frame;
      const bool rendered = this->content_controller_->RenderContent(
          now_ms, id, {}, {}, nullptr, 0, true, false, true, false, &frame);
      if (!rendered || !frame.valid() || frame.size != this->framebuffer_.size())
        return false;
      std::memcpy(this->framebuffer_.data(), frame.data, frame.size);
      return true;
    };
    const auto is_solid = [this](Color color) {
      for (size_t i = 0; i < this->framebuffer_.size(); i += 3) {
        if (this->framebuffer_[i] != color.r ||
            this->framebuffer_[i + 1] != color.g ||
            this->framebuffer_[i + 2] != color.b)
          return false;
      }
      return true;
    };
    const auto is_global_loading = [this]() {
      size_t lit_pixels = 0;
      for (size_t i = 0; i < this->framebuffer_.size(); i += 3) {
        if (this->framebuffer_[i] != 0 || this->framebuffer_[i + 1] != 0 ||
            this->framebuffer_[i + 2] != 0)
          ++lit_pixels;
      }
      const size_t center = (32 * 64 + 32) * 3;
      return lit_pixels >= 30 && lit_pixels <= 60 &&
             this->framebuffer_[center] == 0 &&
             this->framebuffer_[center + 1] == 0 &&
             this->framebuffer_[center + 2] == 0;
    };
    this->content_controller_->HideBaseContent(2000);
    bool preparation_valid =
        render_preparation(2001, "__prepare_outgoing") &&
        is_solid(outgoing.color) &&
        render_preparation(2002, "__prepare_waiting") &&
        is_solid(outgoing.color) && outgoing.hide_count == 0 &&
        waiting.prepare_count == 1 && waiting.show_count == 0;
    waiting.ready = true;
    preparation_valid &= render_preparation(2003, "__prepare_waiting") &&
                         is_solid(waiting.color) && outgoing.hide_count == 1 &&
                         waiting.show_count == 1;
    preparation_valid &= render_preparation(2004, "__prepare_replacement") &&
                         is_solid(waiting.color) &&
                         pending_replacement.prepare_count == 1;
    preparation_valid &= render_preparation(2005, "__prepare_cold") &&
                         pending_replacement.cancel_count == 1 &&
                         cold.prepare_count == 1;
    this->content_controller_->HideBaseContent(2006);
    preparation_valid &= cold.cancel_count == 1;
    preparation_valid &= render_preparation(2007, "__prepare_cold") &&
                         is_global_loading();
    const std::vector<uint8_t> first_loading_frame = this->framebuffer_;
    preparation_valid &= render_preparation(2040, "__prepare_cold") &&
                         is_global_loading() &&
                         this->framebuffer_ != first_loading_frame;

    this->content_controller_->HideBaseContent(2050);
    outgoing.has_presentation = true;
    outgoing.invalidate_on_tick = false;
    waiting.ready = false;
    preparation_valid &= render_preparation(2051, "__prepare_outgoing");
    outgoing.invalidate_on_tick = true;
    const int waiting_cancel_before_loss = waiting.cancel_count;
    preparation_valid &=
        render_preparation(2052, "__prepare_waiting") &&
        waiting.cancel_count == waiting_cancel_before_loss &&
        waiting.show_count == 1 && is_global_loading();
    waiting.ready = true;
    preparation_valid &= render_preparation(2053, "__prepare_waiting") &&
                         is_solid(waiting.color) && waiting.show_count == 2;

    this->content_controller_->HideBaseContent(2060);
    outgoing.has_presentation = true;
    outgoing.invalidate_on_tick = false;
    waiting.ready = false;
    pending_replacement.ready = false;
    preparation_valid &=
        render_preparation(2061, "__prepare_outgoing") &&
        render_preparation(2062, "__prepare_waiting");
    const int waiting_cancel_before_reaction = waiting.cancel_count;
    const int replacement_prepare_before_reaction =
        pending_replacement.prepare_count;
    const int outgoing_hide_before_reaction = outgoing.hide_count;
    pixoo::Overlay reaction_overlay{};
    reaction_overlay.tag = pixoo::OverlayTag::kReaction;
    pixoo::FrameView reaction_frame;
    preparation_valid &= this->content_controller_->RenderContent(
        2063, "__prepare_replacement", {}, {}, &reaction_overlay, 0, true,
        true, false, false, &reaction_frame);
    preparation_valid &=
        waiting.cancel_count == waiting_cancel_before_reaction + 1 &&
        pending_replacement.prepare_count ==
            replacement_prepare_before_reaction + 1 &&
        outgoing.hide_count == outgoing_hide_before_reaction;
    this->content_controller_->HideBaseContent(2064);
    if (!preparation_valid) {
      std::printf("render test: FAILED generic dashboard preparation\n");
      ++failures;
    }

    outgoing.ready = true;
    const uint32_t weather_requests_before =
        this->weather_source_ != nullptr ? this->weather_source_->request_count()
                                         : 0;
    pixoo::FrameView hidden_weather_frame;
    this->content_controller_->RenderContent(
        0, "weather_landscape_loading", {}, {}, nullptr, 0, false, false, true,
        false, &hidden_weather_frame);
    bool weather_preparation_valid =
        this->weather_source_ != nullptr &&
        this->weather_source_->request_count() == weather_requests_before;
    this->content_controller_->HideBaseContent(2100);
    weather_preparation_valid &=
        render_preparation(0, "__prepare_outgoing") &&
        render_preparation(0, "weather_landscape_loading") &&
        this->weather_source_->request_count() > weather_requests_before &&
        is_solid(outgoing.color) &&
        render_preparation(100, "weather_landscape_loading") &&
        !is_solid(outgoing.color) &&
        this->framebuffer_[(31 * 64 + 25) * 3] != 120;
    if (!weather_preparation_valid) {
      std::printf("render test: FAILED weather dashboard preparation\n");
      ++failures;
    }

    if (this->now_playing_source_ != nullptr) {
      this->content_controller_->HideBaseContent(2200);
      bool now_playing_preparation_valid =
          render_preparation(0, "__prepare_outgoing") &&
          render_preparation(0, "now_playing");
      const std::vector<uint8_t> old_now_playing = this->framebuffer_;
      now_playing_preparation_valid &=
          render_preparation(1, "__prepare_outgoing") &&
          render_preparation(6000, "now_playing") && is_solid(outgoing.color) &&
          render_preparation(6300, "now_playing") &&
          this->framebuffer_ != old_now_playing && !is_solid(outgoing.color);
      uint16_t current_artwork[pixoo::now_playing::kArtworkPixelCount];
      now_playing_preparation_valid &=
          this->now_playing_source_->CopyArtwork(5002, 1, current_artwork,
                                                 pixoo::now_playing::kArtworkPixelCount) &&
          this->framebuffer_[0] ==
              static_cast<uint8_t>(((current_artwork[0] >> 11) & 0x1f) * 255 / 31) &&
          this->framebuffer_[1] ==
              static_cast<uint8_t>(((current_artwork[0] >> 5) & 0x3f) * 255 / 63) &&
          this->framebuffer_[2] ==
              static_cast<uint8_t>((current_artwork[0] & 0x1f) * 255 / 31);
      this->content_controller_->HideBaseContent(13199);
      now_playing_preparation_valid &=
          render_preparation(13200, "__prepare_outgoing") &&
          render_preparation(13200, "now_playing") &&
          !is_solid(outgoing.color);

      this->content_controller_->HideBaseContent(25399);
      const uint32_t copies_before_failed_entry =
          this->now_playing_source_->copy_count();
      const int hides_before_failed_entry = outgoing.hide_count;
      now_playing_preparation_valid &=
          render_preparation(25400, "__prepare_outgoing") &&
          render_preparation(25400, "now_playing") &&
          is_solid(outgoing.color) &&
          outgoing.hide_count == hides_before_failed_entry &&
          render_preparation(25600, "now_playing") &&
          !is_solid(outgoing.color) && !is_global_loading() &&
          outgoing.hide_count == hides_before_failed_entry + 1 &&
          this->now_playing_source_->copy_count() == copies_before_failed_entry;
      const std::vector<uint8_t> failed_entry_fallback = this->framebuffer_;
      now_playing_preparation_valid &=
          render_preparation(25601, "now_playing") &&
          this->framebuffer_ == failed_entry_fallback;

      this->content_controller_->HideBaseContent(28999);
      now_playing_preparation_valid &=
          render_preparation(29000, "__prepare_outgoing") &&
          render_preparation(29000, "now_playing") &&
          is_solid(outgoing.color) &&
          render_preparation(30000, "now_playing") &&
          is_solid(outgoing.color) &&
          render_preparation(30050, "now_playing") &&
          is_solid(outgoing.color) &&
          render_preparation(30100, "now_playing") &&
          is_solid(outgoing.color) &&
          render_preparation(30200, "now_playing") &&
          !is_solid(outgoing.color) && !is_global_loading() &&
          render_preparation(30300, "now_playing") &&
          !is_solid(outgoing.color);
      if (!now_playing_preparation_valid) {
        std::printf("render test: FAILED now-playing re-entry preparation\n");
        ++failures;
      }
    }
    std::printf(
        "render test: now-playing object=%zu bytes buffers=%zu bytes "
        "source=%zu bytes\n",
        sizeof(pixoo64::dashboard::NowPlayingDashboard),
        2u * pixoo::now_playing::kArtworkRgb565Bytes,
        sizeof(StaticNowPlayingSource));

#ifdef USE_PIXOO64_NOW_PLAYING
    failures += RunNowPlayingAdapterTests();
#endif

    std::exit(failures == 0 ? 0 : 1);
  });
}

bool RenderTestDisplay::render_frame_(
    uint32_t now_ms, const std::string &dashboard_id,
    const pixoo::Notification *notification,
    uint32_t notification_visible_elapsed_ms, bool base_visible,
    pixoo::StopwatchSnapshot stopwatch, pixoo::TimerSnapshot timer) {
  if (this->content_controller_ == nullptr)
    return false;
  StaticWeatherSource::SetCurrentRenderTime(now_ms);
  StaticNowPlayingSource::SetCurrentRenderTime(now_ms);
  pixoo::Overlay overlay;
  const pixoo::Overlay *overlay_ptr = nullptr;
  if (notification != nullptr) {
    overlay.tag = pixoo::OverlayTag::kNotification;
    overlay.notification = *notification;
    overlay_ptr = &overlay;
  }
  pixoo::FrameView frame;
  if (!this->content_controller_->RenderContent(
          now_ms, dashboard_id, stopwatch, timer, overlay_ptr,
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
                                            bool base_visible,
                                            uint32_t stopwatch_elapsed_ms,
                                            bool stopwatch_running,
                                            uint32_t timer_remaining_ms,
                                            bool timer_running) {
  this->animation_frames_.push_back(AnimationFrame{
      std::move(dashboard_id), now_ms, std::move(snapshot_id), base_visible,
      pixoo::StopwatchSnapshot{stopwatch_elapsed_ms, stopwatch_running},
      pixoo::TimerSnapshot{timer_remaining_ms, timer_running}});
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
