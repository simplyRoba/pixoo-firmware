#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <string>

#include "pixoo_sound.h"
#include "app_state.h"
#include "frame_metrics.h"

namespace pixoo {

// Non-owning view of a renderer-owned framebuffer. PanelPort::Present must
// consume it synchronously before returning; it may copy into adapter-owned
// storage, but must not retain this view. The view remains valid until the
// renderer's next render call.
struct FrameView {
  const uint8_t *data{nullptr};
  size_t size{0};

  bool valid() const { return data != nullptr && size != 0; }
};

// Canonical dashboard metadata resolved only by RenderPort's trusted catalog.
struct DashboardSelection {
  std::string id;
  bool requires_microphone{false};
  uint32_t frame_interval_ms{33};
};

class PanelPort {
 public:
  virtual ~PanelPort() = default;
  virtual void SetPower(bool on) = 0;
  // Duration spent synchronously powering on before the panel can begin its
  // post-power settle delay. Ramping adapters report it after every real
  // SetPower(true) transition, including the first startup transition.
  virtual uint32_t LastPowerOnDelayMs() const { return 0; }
  virtual bool Initialize() = 0;
  virtual void SetBrightness(float brightness) = 0;
  virtual bool Present(FrameView frame, bool force) = 0;
};

class RenderPort {
 public:
  virtual ~RenderPort() = default;

  // Resolves IDs through the renderer's catalog. On failure, leaves selection
  // unchanged; a renderer may instead return its deterministic fallback.
  virtual bool ResolveDashboard(const std::string &requested_id,
                                DashboardSelection *selection) = 0;
  // The renderer owns the reusable backing storage for each returned view.
  virtual FrameView RenderBootAnimation(uint32_t elapsed_ms) = 0;
  // A static screen shown synchronously before the OTA writer begins.
  virtual FrameView RenderFirmwareUpdate() = 0;
  // Notifications may need enough time for their text animation. Reactions
  // have an application-owned fixed duration and do not use this callback.
  virtual uint32_t NotificationMinVisibleMs(
      const Notification &notification) = 0;
  // Ends the current base-dashboard visibility interval. The next rendered
  // base is an entry even if its dashboard selection did not change.
  virtual void HideBaseContent() = 0;
  // Releases renderer-owned storage that is needed only while an overlay is
  // active without ending the base-dashboard visibility interval.
  virtual void ReleaseOverlayResources() = 0;
  // Writes a view on success. `render_base` advances and redraws the selected
  // dashboard; a false value retains the renderer's current base pixels.
  // `base_frozen` tells the renderer that a reaction must freeze one clean
  // base frame. Notifications keep a live base. The overlay is composited
  // only when `render_overlay` is true and must replace every pixel its
  // preceding frame modified. The returned view belongs to the renderer and is
  // consumed synchronously by the caller. `now_ms` is the tick clock, the only
  // time base an animated dashboard has.
  virtual bool RenderContent(uint32_t now_ms, const std::string &dashboard_id,
                             const StopwatchSnapshot &stopwatch,
                             const Overlay *overlay,
                             uint32_t overlay_visible_elapsed_ms,
                             bool base_visible, bool base_frozen,
                             bool render_base, bool render_overlay,
                             FrameView *frame) = 0;
};

class MicrophonePort {
 public:
  virtual ~MicrophonePort() = default;
  virtual void SetEnabled(bool enabled) = 0;
};

// Receives regular dashboard-frame boundaries. Implementations provide the
// clock, so the application core remains independent of platform timing.
class FrameMetricsPort {
 public:
  virtual ~FrameMetricsPort() = default;
  virtual void BeginRegularFrame() = 0;
  virtual void EndRegularFrame() = 0;
};

class LightStateSink {
 public:
  virtual ~LightStateSink() = default;
  virtual void Publish(LightState state) = 0;
};

class SystemPort {
 public:
  virtual ~SystemPort() = default;
  virtual void FactoryReset() = 0;
};

struct NotificationRequest {
  Notification notification;
  uint32_t requested_duration_ms{0};
  bool has_sound{false};
  Sound sound{Sound::kChirp};
};

struct OverlayRequest {
  Overlay overlay{};
  uint32_t requested_duration_ms{0};
  bool has_sound{false};
  Sound sound{Sound::kChirp};
};

constexpr size_t kOverlayQueueCapacity = 16;

// Retained overlay slots are supplied by the platform when memory placement
// matters. The application core keeps this type framework-independent.
struct OverlayQueueStorage {
  std::array<OverlayRequest, kOverlayQueueCapacity> slots{};
};

constexpr uint32_t kNotificationMillisecondsPerSecond = 1000;
constexpr uint32_t kDefaultNotificationDurationSeconds = 4;
constexpr uint32_t kMaximumNotificationDurationSeconds =
    std::numeric_limits<uint32_t>::max() / kNotificationMillisecondsPerSecond;

// Converts the public integer-seconds notification contract to the application's
// internal millisecond duration without overflowing uint32_t.
constexpr uint32_t NotificationDurationMsFromSeconds(int32_t duration_seconds) {
  const uint32_t seconds =
      duration_seconds <= 0
          ? kDefaultNotificationDurationSeconds
          : static_cast<uint32_t>(duration_seconds);
  const uint32_t saturated_seconds =
      seconds > kMaximumNotificationDurationSeconds
          ? kMaximumNotificationDurationSeconds
          : seconds;
  return saturated_seconds * kNotificationMillisecondsPerSecond;
}

struct FirmwareAppConfig {
  uint32_t cold_init_delay_ms{1000};
  uint32_t repower_delay_ms{300};
  uint32_t boot_animation_ms{5000};
  uint32_t boot_frame_interval_ms{33};
  uint32_t overlay_frame_interval_ms{33};
};

class FirmwareApp {
 public:
  enum class Phase {
    kOff,
    kWaitingInit,
    kBootAnimation,
    kRunning,
  };

  // Borrows overlay_queue_storage for this object's lifetime when supplied;
  // otherwise owns ordinary dynamically allocated storage.
  FirmwareApp(PanelPort &panel, RenderPort &renderer,
              SoundPlayer *sound_player = nullptr,
              MicrophonePort *microphone = nullptr,
              LightStateSink *light_sink = nullptr,
              FirmwareAppConfig config = FirmwareAppConfig{},
              SystemPort *system = nullptr,
              FrameMetricsPort *frame_metrics = nullptr,
              OverlayQueueStorage *overlay_queue_storage = nullptr);

  bool Start(uint32_t now_ms, LightState initial_light,
             const std::string &initial_dashboard_id);
  void Tick(uint32_t now_ms);

  // External entity state is inbound user intent. It is never echoed back to
  // the sink, which prevents a publish/callback feedback loop.
  void SetUserLight(LightState light, uint32_t now_ms);

  // Debounced button edges enter the application without gesture meaning.
  // Timing and action selection remain native, host-tested product policy.
  void PowerButtonPressed(uint32_t now_ms);
  void PowerButtonReleased(uint32_t now_ms);
  void BrightnessButtonPressed(uint32_t now_ms);
  void BrightnessButtonReleased(uint32_t now_ms);

  void TogglePower(uint32_t now_ms);
  void StepBrightness(uint32_t now_ms);

  void SelectDashboard(const std::string &dashboard_id);
  void StopwatchStart(uint32_t now_ms);
  void StopwatchStop(uint32_t now_ms);
  void StopwatchReset(uint32_t now_ms);
  StopwatchSnapshot stopwatch() const { return this->stopwatch_; }
  // Renders and force-presents the update frame synchronously before OTA
  // proceeds. Returns false while the panel is not ready or presentation fails.
  bool BeginFirmwareUpdate();
  // Accepted overlays are presented in strict FIFO order. The current overlay
  // counts toward the bounded capacity; a full queue rejects the new tail.
  bool Notify(NotificationRequest request, uint32_t now_ms);
  bool React(Reaction reaction, uint32_t now_ms);
  void ClearOverlayQueue();
  void FactoryReset();

  Phase phase() const { return this->phase_; }
  const LightState &logical_light() const { return this->logical_light_; }
  const DashboardSelection &selected_dashboard() const {
    return this->selected_dashboard_;
  }
  size_t overlay_queue_size() const { return this->overlay_queue_size_; }
  bool overlay_pending() const {
    return this->overlay_queue_size_ != 0 && !this->overlay_visible_;
  }
  bool overlay_visible() const { return this->overlay_visible_; }
  const Overlay *current_overlay() const;
  bool notification_pending() const;
  bool notification_visible() const;

 private:
  static float ClampBrightness_(float brightness);
  void SyncBrightnessBounce_(float brightness);
  static bool ElapsedAtLeast_(uint32_t now_ms, uint32_t started_ms,
                              uint32_t duration_ms);
  void AdvanceStopwatch_(uint32_t now_ms);

  void ApplyUserLight_(LightState light, uint32_t now_ms, bool publish);
  bool EnqueueOverlay_(OverlayRequest request, uint32_t now_ms);
  void PromoteOverlay_();
  bool CancelOverlayQueueWithoutRestore_();
  bool StopOverlaySound_();
  void ResetCurrentOverlayPresentation_();
  void ResetOverlayState_();
  void RestoreOverlaySnapshot_();
  const OverlayRequest &CurrentOverlayRequest_() const;
  void BeginWaitingInit_(uint32_t now_ms, uint32_t delay_ms);
  void BeginRepowerWaiting_(uint32_t now_ms);
  void RenderRunning_(uint32_t now_ms);
  bool BaseContentVisible_() const;
  void ReconcileMicrophone_();
  bool BaseRenderDue_(uint32_t now_ms) const;
  bool OverlayRenderDue_(uint32_t now_ms) const;
  bool BootRenderDue_(uint32_t now_ms) const;
  void RequestVisibleRender_();
  static uint32_t ValidFrameInterval_(uint32_t interval_ms);
  static bool DeadlineReached_(uint32_t now_ms, uint32_t deadline_ms);
  static uint32_t NextDeadline_(uint32_t now_ms, uint32_t deadline_ms,
                                uint32_t interval_ms, bool reset_phase);

  PanelPort &panel_;
  RenderPort &renderer_;
  SoundPlayer *sound_player_;
  MicrophonePort *microphone_;
  LightStateSink *light_sink_;
  FirmwareAppConfig config_;
  SystemPort *system_;
  FrameMetricsPort *frame_metrics_;

  Phase phase_{Phase::kOff};
  LightState logical_light_{};
  DashboardSelection selected_dashboard_{};
  uint32_t phase_started_ms_{0};
  uint32_t phase_delay_ms_{0};
  uint32_t boot_started_ms_{0};
  uint32_t next_boot_render_ms_{0};
  uint32_t next_base_render_ms_{0};
  uint32_t next_overlay_render_ms_{0};
  bool boot_rendered_{false};
  bool base_rendered_{false};
  bool overlay_rendered_{false};
  bool base_render_requested_{true};
  bool overlay_render_requested_{false};
  // Consumed when the first successful initialization enters the boot animation.
  bool first_boot_pending_{true};
  bool boot_sound_played_{false};
  bool microphone_enabled_{false};

  StopwatchSnapshot stopwatch_{};
  uint32_t stopwatch_last_updated_ms_{0};

  // Current bounce point and direction, synchronized with external light
  // state. The endpoints reverse; middle steps head upward deterministically.
  int brightness_step_{3};
  int brightness_direction_{-1};

  bool power_button_pressed_{false};
  uint32_t power_button_pressed_at_ms_{0};
  bool brightness_button_pressed_{false};
  uint32_t brightness_button_pressed_at_ms_{0};

  std::unique_ptr<OverlayQueueStorage> owned_overlay_queue_storage_;
  OverlayQueueStorage *overlay_queue_storage_{nullptr};
  size_t overlay_queue_head_{0};
  size_t overlay_queue_size_{0};
  bool overlay_visible_{false};
  bool overlay_sound_started_{false};
  LightState overlay_saved_light_{};
  uint32_t overlay_visible_started_ms_{0};
  uint32_t overlay_visible_duration_ms_{0};
};

}  // namespace pixoo
