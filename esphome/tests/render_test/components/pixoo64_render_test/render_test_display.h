#pragma once

#include <string>
#include <vector>

#include "esphome/components/pixoo64_content/content_controller.h"
#include "dashboard/equalizer/equalizer_dashboard.h"
#include "dashboard/now_playing/now_playing_dashboard.h"
#include "esphome/components/display/display.h"
#include "esphome/components/text/text.h"
#include "static_now_playing_source.h"
#include "static_weather_source.h"

namespace esphome::pixoo64_render_test {

class RenderTestDisplay final : public display::Display {
 public:
  void setup() override;
  void update() override {}
  void set_content_controller(
      pixoo64::content::ContentController *controller);
  void set_equalizer_dashboard(pixoo64::dashboard::EqualizerDashboard *eq) {
    this->equalizer_ = eq;
  }
  void set_notification_font(font::Font *font) { this->notification_font_ = font; }
  void set_panel_text(text::Text *text) { this->text_ = text; }
  void set_now_playing_source(StaticNowPlayingSource *source) {
    this->now_playing_source_ = source;
  }
  void set_now_playing_dashboard(pixoo64::dashboard::NowPlayingDashboard *dashboard) {
    this->now_playing_dashboard_ = dashboard;
  }
  void set_weather_source(StaticWeatherSource *source) {
    this->weather_source_ = source;
  }
  void set_output_dir(std::string dir);
  void set_animation_only(bool animation_only) {
    this->animation_only_ = animation_only;
  }
  // Renders `dashboard_id` at tick time `now_ms`, after single-frame dashboards.
  // Empty snapshot IDs skip comparison; advance_only ticks the now-playing
  // dashboard without drawing. Animated dashboards get no single frame.
  void add_animation_frame(std::string dashboard_id, uint32_t now_ms,
                           std::string snapshot_id, bool base_visible,
                           pixoo::TimingSnapshot timing, bool advance_only = false);
  display::DisplayType get_display_type() override {
    return display::DISPLAY_TYPE_COLOR;
  }
  void draw_pixel_at(int x, int y, Color color) override;

 protected:
  int get_width_internal() override { return 64; }
  int get_height_internal() override { return 64; }
  struct AnimationFrame {
    std::string dashboard_id;
    uint32_t now_ms{0};
    std::string snapshot_id;
    bool base_visible{true};
    pixoo::TimingSnapshot timing{};
    bool advance_only{false};
  };

  bool run_animation_frame_(const AnimationFrame &frame);
  bool render_frame_(uint32_t now_ms, const std::string &dashboard_id,
                     const pixoo::Notification *notification,
                     uint32_t notification_visible_elapsed_ms,
                     bool base_visible,
                     pixoo::TimingSnapshot timing = {});
  bool has_animation_frames_(const std::string &dashboard_id) const;

  pixoo64::content::ContentController *content_controller_{nullptr};
  pixoo64::dashboard::EqualizerDashboard *equalizer_{nullptr};
  text::Text *text_{nullptr};
  font::Font *notification_font_{nullptr};
  StaticNowPlayingSource *now_playing_source_{nullptr};
  pixoo64::dashboard::NowPlayingDashboard *now_playing_dashboard_{nullptr};
  StaticWeatherSource *weather_source_{nullptr};
  std::string output_dir_;
  std::vector<AnimationFrame> animation_frames_;
  bool animation_only_{false};
  std::vector<uint8_t> framebuffer_ = std::vector<uint8_t>(64 * 64 * 3, 0);
};

}  // namespace esphome::pixoo64_render_test
