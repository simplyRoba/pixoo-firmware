#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "blend_canvas.h"
#include "dashboard/clock/clock_dashboard.h"
#include "dashboard/dashboard.h"
#include "dashboard/equalizer/equalizer_dashboard.h"
#include "dashboard/game_of_life/game_of_life_dashboard.h"
#include "dashboard/now_playing/now_playing_dashboard.h"
#include "dashboard/timing_dashboard.h"
#include "esphome/components/display/display.h"
#include "esphome/components/sensor/sensor.h"
#include "firmware_app.h"
#include "notification/notification_renderer.h"
#include "pixoo_framebuffer.h"
#include "reaction/reaction_renderer.h"
#include "dashboard/text/text_dashboard.h"
#include "dashboard/weather/weather_dashboard.h"

namespace esphome::pixoo64::content {

using dashboard::Dashboard;

class ContentController : public display::Display,
                         public pixoo::RenderPort,
                         public pixoo::EqualizerLevelsSink,
                         public BlendCanvas {
 public:
  void add_dashboard(Dashboard *dashboard);
  void set_stopwatch_dashboard(dashboard::TimingDashboard *dashboard) {
    this->stopwatch_dashboard_ = dashboard;
  }
  void set_timer_dashboard(dashboard::TimingDashboard *dashboard) {
    this->timer_dashboard_ = dashboard;
  }
  void set_default_dashboard(std::string id);
  std::vector<std::string> dashboard_ids() const;

  void set_notification_font(font::Font *font) {
    this->notification_renderer_.set_font(font);
  }
  void set_firmware_update_title_font(font::Font *font) {
    this->firmware_update_title_font_ = font;
  }
  void set_firmware_update_detail_font(font::Font *font) {
    this->firmware_update_detail_font_ = font;
  }
  void set_render_budget_us(uint32_t budget_us) {
    this->render_budget_us_ = budget_us;
  }
  void set_render_average_sensor(sensor::Sensor *sensor) {
    this->render_average_sensor_ = sensor;
  }
  void set_render_max_sensor(sensor::Sensor *sensor) {
    this->render_max_sensor_ = sensor;
  }
  void set_render_over_budget_sensor(sensor::Sensor *sensor) {
    this->render_over_budget_sensor_ = sensor;
  }
  void update() override;
  display::DisplayType get_display_type() override {
    return display::DISPLAY_TYPE_COLOR;
  }
  void draw_pixel_at(int x, int y, Color color) override;
  void BlendPixel(int x, int y, Color color, float alpha) override;

  bool ResolveDashboard(const std::string &requested_id,
                        pixoo::DashboardSelection *selection) override;
  // Fans captured spectrum levels out to every equalizer dashboard, so each
  // face stays live regardless of which one is currently selected.
  void SetLevels(const float levels[pixoo::kBands]) override;
  pixoo::FrameView RenderBootAnimation(uint32_t elapsed_ms) override;
  pixoo::FrameView RenderFirmwareUpdate() override;
  uint32_t NotificationMinVisibleMs(
      const pixoo::Notification &notification) override;
  void HideBaseContent(uint32_t now_ms) override;
  void ReleaseOverlayResources() override;
  bool RenderContent(uint32_t now_ms, const std::string &dashboard_id,
                     const pixoo::StopwatchSnapshot &stopwatch,
                     const pixoo::TimerSnapshot &timer,
                     const pixoo::Overlay *overlay,
                     uint32_t overlay_visible_elapsed_ms, bool base_visible,
                     bool base_frozen, bool render_base, bool render_overlay,
                     pixoo::FrameView *frame) override;

 protected:
  int get_width_internal() override { return pixoo::kWidth; }
  int get_height_internal() override { return pixoo::kHeight; }

  Dashboard *find_(const std::string &id) const;
  void HideVisible_(uint32_t now_ms);
  void DrawBootWordmark(uint32_t elapsed_ms);
  static int cos_deg_(int degrees);
  bool CaptureReactionBackground_();
  bool EnsureReactionBackground_();
  void ReleaseReactionBackground_();
  void RecordRender_(uint32_t elapsed_us);
  void PublishRenderWindow_();
  void ResetRenderWindow_();

  std::vector<Dashboard *> dashboards_;
  Dashboard *visible_{nullptr};
  dashboard::TimingDashboard *stopwatch_dashboard_{nullptr};
  dashboard::TimingDashboard *timer_dashboard_{nullptr};
  std::string default_dashboard_id_;
  struct ReactionBackgroundDeleter {
    void operator()(pixoo::Framebuffer *framebuffer) const;
  };

  pixoo::Framebuffer framebuffer_;
  std::unique_ptr<pixoo::Framebuffer, ReactionBackgroundDeleter>
      reaction_background_;
  NotificationRenderer notification_renderer_;
  ReactionRenderer reaction_renderer_;
  const pixoo::Overlay *reaction_overlay_{nullptr};
  uint32_t last_reaction_elapsed_ms_{0};
  bool reaction_background_allocation_attempted_{false};
  bool reaction_snapshot_active_{false};
  font::Font *firmware_update_title_font_{nullptr};
  font::Font *firmware_update_detail_font_{nullptr};
  sensor::Sensor *render_average_sensor_{nullptr};
  sensor::Sensor *render_max_sensor_{nullptr};
  sensor::Sensor *render_over_budget_sensor_{nullptr};
  uint32_t render_budget_us_{0};
  uint32_t render_frames_{0};
  uint32_t render_over_budget_{0};
  uint32_t render_max_us_{0};
  uint64_t render_total_us_{0};
};

}  // namespace esphome::pixoo64::content
