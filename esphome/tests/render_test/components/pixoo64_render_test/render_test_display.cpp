#include "render_test_display.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <numeric>
#include <utility>

#include "dashboard/now_playing/now_playing_dashboard.h"
#include "dashboard/weather/weather_icon.h"
#include "esphome/components/pixoo64_content/blend_canvas.h"
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
                                       "now_playing_paused_midpoint");
    now_playing_valid &= frames_differ("now_playing_paused_midpoint",
                                       "now_playing_paused");
    now_playing_valid &= frames_differ("now_playing_paused",
                                       "now_playing_buffering");
    // Pending media keeps the old pixels and rows. A ready replacement begins
    // at that same image, crosses through a distinct blend, then reaches the
    // replacement image rather than cutting to it.
    now_playing_valid &= cover_pixels_equal(
        "now_playing_track_change_old", "now_playing_track_change_pending");
    now_playing_valid &= frame_rows_equal(
        "now_playing_track_change_old", "now_playing_track_change_pending", 45, 18);
    now_playing_valid &= cover_pixels_equal(
        "now_playing_track_change_pending",
        "now_playing_track_change_ready_start");
    now_playing_valid &= frame_rows_equal(
        "now_playing_track_change_pending",
        "now_playing_track_change_ready_start", 45, 18);
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
                  this->now_playing_source_->copy_count() == 8;
    if (!now_playing_lifecycle_valid) {
      std::printf("render test: FAILED now-playing source lifecycle\n");
      ++failures;
    }

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
          is_solid(outgoing.color) &&
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
