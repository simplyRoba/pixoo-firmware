#pragma once

#include <string>
#include <vector>

#include "esphome/components/pixoo64_content/content_controller.h"
#include "dashboard/equalizer/equalizer_dashboard.h"
#include "esphome/components/display/display.h"
#include "esphome/components/text/text.h"
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
  void set_panel_text(text::Text *text) { this->text_ = text; }
  void set_output_dir(std::string dir);
  // Renders `dashboard_id` at tick time `now_ms`, in the order added and after
  // the single-frame dashboards. An empty `snapshot_id` only advances the
  // animation. A dashboard with animation frames gets no single frame.
  void add_animation_frame(std::string dashboard_id, uint32_t now_ms,
                           std::string snapshot_id, bool base_visible,
                           uint32_t stopwatch_elapsed_ms,
                           bool stopwatch_running);
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
    pixoo::StopwatchSnapshot stopwatch{};
  };

  bool render_frame_(uint32_t now_ms, const std::string &dashboard_id,
                     const pixoo::Notification *notification,
                     uint32_t notification_visible_elapsed_ms,
                     bool base_visible,
                     pixoo::StopwatchSnapshot stopwatch = {});
  bool has_animation_frames_(const std::string &dashboard_id) const;

  pixoo64::content::ContentController *content_controller_{nullptr};
  pixoo64::dashboard::EqualizerDashboard *equalizer_{nullptr};
  text::Text *text_{nullptr};
  std::string output_dir_;
  std::vector<AnimationFrame> animation_frames_;
  std::vector<uint8_t> framebuffer_ = std::vector<uint8_t>(64 * 64 * 3, 0);
};

}  // namespace esphome::pixoo64_render_test
