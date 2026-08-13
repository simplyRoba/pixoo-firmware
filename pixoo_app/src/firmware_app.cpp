#include "firmware_app.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace pixoo {
namespace {

constexpr uint32_t kShortPressMinimumMs = 50;
constexpr uint32_t kShortPressMaximumMs = 2000;
constexpr uint32_t kFactoryResetMinimumMs = 10000;
constexpr uint32_t kFactoryResetMaximumMs = 60000;

bool IsDurationInRange(uint32_t duration_ms, uint32_t minimum_ms,
                       uint32_t maximum_ms) {
  return duration_ms >= minimum_ms && duration_ms <= maximum_ms;
}

}  // namespace

FirmwareApp::FirmwareApp(PanelPort &panel, RenderPort &renderer,
                         SoundPlayer *sound_player,
                         MicrophonePort *microphone,
                         LightStateSink *light_sink, FirmwareAppConfig config,
                         SystemPort *system, FrameMetricsPort *frame_metrics,
                         OverlayQueueStorage *overlay_queue_storage)
    : panel_(panel),
      renderer_(renderer),
      sound_player_(sound_player),
      microphone_(microphone),
      light_sink_(light_sink),
      config_(config),
      system_(system),
      frame_metrics_(frame_metrics),
      owned_overlay_queue_storage_(
          overlay_queue_storage == nullptr ? new OverlayQueueStorage : nullptr),
      overlay_queue_storage_(overlay_queue_storage) {
  if (this->overlay_queue_storage_ == nullptr)
    this->overlay_queue_storage_ = this->owned_overlay_queue_storage_.get();
}

bool FirmwareApp::ElapsedAtLeast_(uint32_t now_ms, uint32_t started_ms,
                                  uint32_t duration_ms) {
  return now_ms - started_ms >= duration_ms;
}

uint32_t FirmwareApp::ValidFrameInterval_(uint32_t interval_ms) {
  return interval_ms == 0 ? 1 : interval_ms;
}

bool FirmwareApp::DeadlineReached_(uint32_t now_ms, uint32_t deadline_ms) {
  return static_cast<int32_t>(now_ms - deadline_ms) >= 0;
}

uint32_t FirmwareApp::NextDeadline_(uint32_t now_ms, uint32_t deadline_ms,
                                    uint32_t interval_ms, bool reset_phase) {
  interval_ms = ValidFrameInterval_(interval_ms);
  if (reset_phase)
    return now_ms + interval_ms;

  const uint32_t elapsed_ms = now_ms - deadline_ms;
  const uint32_t missed_intervals = elapsed_ms / interval_ms;
  return deadline_ms + (missed_intervals + 1) * interval_ms;
}

bool FirmwareApp::BootRenderDue_(uint32_t now_ms) const {
  return !this->boot_rendered_ ||
         DeadlineReached_(now_ms, this->next_boot_render_ms_);
}

bool FirmwareApp::BaseRenderDue_(uint32_t now_ms) const {
  return this->base_render_requested_ || !this->base_rendered_ ||
         DeadlineReached_(now_ms, this->next_base_render_ms_);
}

bool FirmwareApp::OverlayRenderDue_(uint32_t now_ms) const {
  return this->overlay_render_requested_ || !this->overlay_rendered_ ||
         DeadlineReached_(now_ms, this->next_overlay_render_ms_);
}

void FirmwareApp::RequestVisibleRender_() {
  if (this->overlay_queue_size_ != 0)
    this->overlay_render_requested_ = true;
  else
    this->base_render_requested_ = true;
}

float FirmwareApp::ClampBrightness_(float brightness) {
  if (!std::isfinite(brightness))
    return 0.0f;
  if (brightness < 0.0f)
    return 0.0f;
  if (brightness > 1.0f)
    return 1.0f;
  return brightness;
}

void FirmwareApp::SyncBrightnessBounce_(float brightness) {
  brightness_step_ = std::clamp(
      static_cast<int>(std::lround(ClampBrightness_(brightness) * 4.0f)) - 1,
      0, 3);
  brightness_direction_ = brightness_step_ == 3 ? -1 : 1;
}

bool FirmwareApp::Start(uint32_t now_ms, LightState initial_light,
                        const std::string &initial_dashboard_id) {
  // A restarted application must not leave an earlier boot or overlay sound
  // running into its new lifecycle.
  this->StopSound_();
  this->phase_ = Phase::kOff;
  this->stopwatch_ = {};
  this->stopwatch_last_updated_ms_ = now_ms;
  this->timer_ = {};
  this->timer_loaded_duration_ms_ = 0;
  this->timer_last_updated_ms_ = now_ms;
  this->first_boot_pending_ = true;
  this->boot_sound_played_ = false;
  this->power_button_pressed_ = false;
  this->brightness_button_pressed_ = false;
  this->boot_rendered_ = false;
  this->base_rendered_ = false;
  this->overlay_rendered_ = false;
  this->base_render_requested_ = true;
  this->overlay_render_requested_ = false;
  this->ResetOverlayState_();
  this->ReconcileMicrophone_();

  this->logical_light_ = initial_light;
  this->logical_light_.brightness =
      ClampBrightness_(this->logical_light_.brightness);
  this->SyncBrightnessBounce_(this->logical_light_.brightness);

  DashboardSelection initial_selection;
  if (!this->renderer_.ResolveDashboard(initial_dashboard_id,
                                        &initial_selection) ||
      initial_selection.id.empty()) {
    this->selected_dashboard_ = {};
    this->panel_.SetBrightness(this->logical_light_.brightness);
    this->panel_.SetPower(false);
    this->ReconcileMicrophone_();
    return false;
  }
  initial_selection.frame_interval_ms =
      ValidFrameInterval_(initial_selection.frame_interval_ms);
  this->selected_dashboard_ = std::move(initial_selection);
  this->panel_.SetBrightness(this->logical_light_.brightness);

  if (!this->logical_light_.on) {
    this->panel_.SetPower(false);
    this->ReconcileMicrophone_();
    return true;
  }

  this->panel_.SetPower(true);
  this->BeginWaitingInit_(now_ms, this->config_.cold_init_delay_ms +
                                     this->panel_.LastPowerOnDelayMs());
  return true;
}

void FirmwareApp::BeginWaitingInit_(uint32_t now_ms, uint32_t delay_ms) {
  this->phase_ = Phase::kWaitingInit;
  this->phase_started_ms_ = now_ms;
  this->phase_delay_ms_ = delay_ms;
  this->ReconcileMicrophone_();
}

void FirmwareApp::BeginRepowerWaiting_(uint32_t now_ms) {
  this->BeginWaitingInit_(now_ms, this->config_.repower_delay_ms +
                                      this->panel_.LastPowerOnDelayMs());
}

void FirmwareApp::Tick(uint32_t now_ms) {
  this->AdvanceStopwatch_(now_ms);
  this->AdvanceTimer_(now_ms);
  switch (this->phase_) {
    case Phase::kOff:
      return;

    case Phase::kWaitingInit:
      if (!ElapsedAtLeast_(now_ms, this->phase_started_ms_,
                           this->phase_delay_ms_))
        return;
      if (!this->panel_.Initialize())
        return;
      if (this->first_boot_pending_) {
        this->first_boot_pending_ = false;
        this->phase_ = Phase::kBootAnimation;
        this->boot_started_ms_ = now_ms;
        this->boot_rendered_ = false;
        if (!this->boot_sound_played_) {
          // A countdown may finish while the panel is still initializing. Its
          // alarm takes precedence over the optional startup chime.
          if (this->sound_owner_ != SoundOwner::kTimer)
            this->PlaySound_(Sound::kBoot, SoundOwner::kBoot);
          this->boot_sound_played_ = true;
        }
      } else {
        this->phase_ = Phase::kRunning;
        this->base_render_requested_ = true;
      }
      this->ReconcileMicrophone_();
      return;

    case Phase::kBootAnimation: {
      const uint32_t elapsed_ms = now_ms - this->boot_started_ms_;
      if (elapsed_ms < this->config_.boot_animation_ms) {
        if (!this->BootRenderDue_(now_ms))
          return;
        const bool reset_boot_phase = !this->boot_rendered_;
        this->boot_rendered_ = true;
        this->next_boot_render_ms_ = NextDeadline_(
            now_ms, this->next_boot_render_ms_,
            this->config_.boot_frame_interval_ms, reset_boot_phase);
        const FrameView frame = this->renderer_.RenderBootAnimation(elapsed_ms);
        if (frame.valid())
          this->panel_.Present(frame, false);
        return;
      }
      this->phase_ = Phase::kRunning;
      this->base_render_requested_ = true;
      this->ReconcileMicrophone_();
      break;
    }

    case Phase::kRunning:
      break;
  }

  this->RenderRunning_(now_ms);
}

void FirmwareApp::AdvanceStopwatch_(uint32_t now_ms) {
  if (!this->stopwatch_.running)
    return;
  const uint32_t elapsed = now_ms - this->stopwatch_last_updated_ms_;
  const uint32_t remaining = kStopwatchMaximumElapsedMs - this->stopwatch_.elapsed_ms;
  if (elapsed >= remaining) {
    this->stopwatch_.elapsed_ms = kStopwatchMaximumElapsedMs;
    this->stopwatch_.running = false;
  } else {
    this->stopwatch_.elapsed_ms += elapsed;
  }
  this->stopwatch_last_updated_ms_ = now_ms;
}

void FirmwareApp::PlaySound_(Sound sound, SoundOwner owner) {
  if (this->sound_player_ == nullptr) {
    this->sound_owner_ = SoundOwner::kNone;
    return;
  }
  this->sound_player_->Play(sound);
  this->sound_owner_ = owner;
}

void FirmwareApp::StopSound_() {
  if (this->sound_player_ != nullptr)
    this->sound_player_->Stop();
  this->sound_owner_ = SoundOwner::kNone;
}

bool FirmwareApp::StopSoundIfOwned_(SoundOwner owner) {
  if (this->sound_owner_ != owner)
    return false;
  if (this->sound_player_ != nullptr)
    this->sound_player_->Stop();
  this->sound_owner_ = SoundOwner::kNone;
  return true;
}

void FirmwareApp::StopTimerAlarm_() {
  this->StopSoundIfOwned_(SoundOwner::kTimer);
}

void FirmwareApp::AdvanceTimer_(uint32_t now_ms) {
  if (!this->timer_.running)
    return;
  const uint32_t elapsed = now_ms - this->timer_last_updated_ms_;
  if (elapsed >= this->timer_.remaining_ms) {
    this->timer_.remaining_ms = 0;
    this->timer_.running = false;
    this->timer_last_updated_ms_ = now_ms;
    this->PlaySound_(Sound::kAlarm1, SoundOwner::kTimer);
    return;
  }
  this->timer_.remaining_ms -= elapsed;
  this->timer_last_updated_ms_ = now_ms;
}

bool FirmwareApp::BaseContentVisible_() const {
  return this->phase_ == Phase::kRunning && this->logical_light_.on &&
         (this->overlay_queue_size_ == 0 || this->overlay_saved_light_.on);
}

void FirmwareApp::ReconcileMicrophone_() {
  const bool should_enable = this->phase_ == Phase::kRunning &&
                             this->logical_light_.on &&
                             this->selected_dashboard_.requires_microphone &&
                             this->BaseContentVisible_();
  if (should_enable == this->microphone_enabled_)
    return;
  this->microphone_enabled_ = should_enable;
  if (this->microphone_ != nullptr)
    this->microphone_->SetEnabled(should_enable);
}

void FirmwareApp::RenderRunning_(uint32_t now_ms) {
  if (this->overlay_queue_size_ != 0 && this->overlay_visible_ &&
      ElapsedAtLeast_(now_ms, this->overlay_visible_started_ms_,
                      this->overlay_visible_duration_ms_)) {
    if (this->overlay_queue_size_ == 1) {
      this->RestoreOverlaySnapshot_();
      if (this->phase_ == Phase::kOff)
        return;
    } else {
      this->PromoteOverlay_();
    }
  }

  const bool base_frozen =
      this->overlay_queue_size_ != 0 &&
      this->CurrentOverlayRequest_().overlay.tag == OverlayTag::kReaction;
  // A reaction never uses retained dashboard pixels: its first presented frame
  // draws a clean base, then the renderer freezes and blurs that frame.
  const bool initial_reaction_base = base_frozen && !this->overlay_visible_;
  const bool render_base =
      this->BaseContentVisible_() &&
      (initial_reaction_base ||
       (!base_frozen && this->BaseRenderDue_(now_ms)));
  if (this->overlay_queue_size_ == 0) {
    if (!render_base)
      return;
    const bool reset_base_phase =
        this->base_render_requested_ || !this->base_rendered_;
    this->base_rendered_ = true;
    this->base_render_requested_ = false;
    this->next_base_render_ms_ = NextDeadline_(
        now_ms, this->next_base_render_ms_,
        this->selected_dashboard_.frame_interval_ms, reset_base_phase);
    FrameView frame;
    if (this->frame_metrics_ != nullptr)
      this->frame_metrics_->BeginRegularFrame();
    const bool rendered = this->renderer_.RenderContent(
        now_ms, this->selected_dashboard_.id, this->stopwatch_, this->timer_,
        nullptr, 0, true, false, true, false, &frame);
    if (rendered && frame.valid())
      this->panel_.Present(frame, false);
    if (this->frame_metrics_ != nullptr)
      this->frame_metrics_->EndRegularFrame();
    return;
  }

  const bool render_overlay = this->OverlayRenderDue_(now_ms);
  if (!render_base && !render_overlay)
    return;

  if (render_base) {
    const bool reset_base_phase =
        this->base_render_requested_ || !this->base_rendered_;
    this->base_rendered_ = true;
    this->base_render_requested_ = false;
    this->next_base_render_ms_ = NextDeadline_(
        now_ms, this->next_base_render_ms_,
        this->selected_dashboard_.frame_interval_ms, reset_base_phase);
  }
  if (render_overlay) {
    const bool reset_overlay_phase =
        this->overlay_render_requested_ || !this->overlay_rendered_;
    this->overlay_rendered_ = true;
    this->overlay_render_requested_ = false;
    this->next_overlay_render_ms_ = NextDeadline_(
        now_ms, this->next_overlay_render_ms_,
        this->config_.overlay_frame_interval_ms, reset_overlay_phase);
  }

  const uint32_t visible_elapsed_ms =
      this->overlay_visible_ ? now_ms - this->overlay_visible_started_ms_ : 0;
  const OverlayRequest &request = this->CurrentOverlayRequest_();
  FrameView frame;
  if (render_overlay && this->frame_metrics_ != nullptr)
    this->frame_metrics_->BeginRegularFrame();
  const bool rendered = this->renderer_.RenderContent(
      now_ms, this->selected_dashboard_.id, this->stopwatch_, this->timer_,
      &request.overlay, visible_elapsed_ms, this->overlay_saved_light_.on,
      base_frozen, render_base, render_overlay, &frame);
  const bool presented = render_overlay && rendered && frame.valid() &&
                         this->panel_.Present(frame, !this->overlay_visible_);
  if (render_overlay && this->frame_metrics_ != nullptr)
    this->frame_metrics_->EndRegularFrame();
  if (!render_overlay || !presented || this->overlay_visible_)
    return;

  this->overlay_visible_ = true;
  this->overlay_visible_started_ms_ = now_ms;
  if (request.overlay.tag == OverlayTag::kNotification) {
    this->overlay_visible_duration_ms_ = std::max(
        request.requested_duration_ms,
        this->renderer_.NotificationMinVisibleMs(request.overlay.notification));
  } else {
    this->overlay_visible_duration_ms_ =
        ReactionVisibleDurationMs(request.overlay.reaction);
  }

  if (request.has_sound) {
    this->PlaySound_(request.sound, SoundOwner::kOverlay);
    this->overlay_sound_started_ = true;
  }
}

void FirmwareApp::SetUserLight(LightState light, uint32_t now_ms) {
  light.brightness = ClampBrightness_(light.brightness);
  if (light.brightness != this->logical_light_.brightness)
    this->SyncBrightnessBounce_(light.brightness);
  this->ApplyUserLight_(light, now_ms, false);
}

void FirmwareApp::ApplyUserLight_(LightState light, uint32_t now_ms,
                                  bool publish) {
  light.brightness = ClampBrightness_(light.brightness);
  const bool power_changed = light.on != this->logical_light_.on;
  const bool brightness_changed =
      light.brightness != this->logical_light_.brightness;
  const bool had_overlay = this->overlay_queue_size_ != 0;
  // While an overlay has temporarily woken a logically-off panel, an explicit
  // off command repeats the off state. A changed brightness still means only a
  // brightness command and leaves the queue running.
  const bool repeated_off_command =
      had_overlay && !light.on && !power_changed && !brightness_changed;
  const bool overlay_sound_stopped =
      had_overlay && (power_changed || repeated_off_command) &&
      this->CancelOverlayQueueWithoutRestore_();

  this->logical_light_ = light;
  // Brightness is user state, not an overlay cancellation signal. Preserve it
  // as the value restored after the final queued overlay.
  if (!power_changed && brightness_changed && this->overlay_queue_size_ != 0)
    this->overlay_saved_light_.brightness = light.brightness;
  this->panel_.SetBrightness(light.brightness);

  if (!light.on && this->overlay_queue_size_ == 0) {
    // An explicit logical off is a product-level mute, including a boot chime
    // that is unrelated to any overlay. An off-state brightness update during
    // a temporary overlay wake leaves the panel and queue running.
    if (!overlay_sound_stopped)
      this->StopSound_();
    this->renderer_.HideBaseContent();
    this->panel_.SetPower(false);
    this->phase_ = Phase::kOff;
  } else if (light.on && this->phase_ == Phase::kOff) {
    this->panel_.SetPower(true);
    this->BeginRepowerWaiting_(now_ms);
  } else {
    this->ReconcileMicrophone_();
  }

  // A queue that woke an off panel has already powered it and may be
  // initializing or running. User-on makes base content authoritative without
  // repeating either operation; the next running tick renders that base.
  if ((power_changed || brightness_changed) && light.on &&
      this->phase_ == Phase::kRunning)
    this->base_render_requested_ = true;
  this->ReconcileMicrophone_();
  if (publish && this->light_sink_ != nullptr)
    this->light_sink_->Publish(this->logical_light_);
}

void FirmwareApp::PowerButtonPressed(uint32_t now_ms) {
  if (this->power_button_pressed_)
    return;
  this->power_button_pressed_ = true;
  this->power_button_pressed_at_ms_ = now_ms;
}

void FirmwareApp::PowerButtonReleased(uint32_t now_ms) {
  if (!this->power_button_pressed_)
    return;
  this->power_button_pressed_ = false;
  const uint32_t duration_ms = now_ms - this->power_button_pressed_at_ms_;
  if (IsDurationInRange(duration_ms, kShortPressMinimumMs,
                        kShortPressMaximumMs)) {
    this->TogglePower(now_ms);
  } else if (IsDurationInRange(duration_ms, kFactoryResetMinimumMs,
                               kFactoryResetMaximumMs)) {
    this->FactoryReset();
  }
}

void FirmwareApp::BrightnessButtonPressed(uint32_t now_ms) {
  if (this->brightness_button_pressed_)
    return;
  this->brightness_button_pressed_ = true;
  this->brightness_button_pressed_at_ms_ = now_ms;
}

void FirmwareApp::BrightnessButtonReleased(uint32_t now_ms) {
  if (!this->brightness_button_pressed_)
    return;
  this->brightness_button_pressed_ = false;
  const uint32_t duration_ms = now_ms - this->brightness_button_pressed_at_ms_;
  if (IsDurationInRange(duration_ms, kShortPressMinimumMs,
                        kShortPressMaximumMs))
    this->StepBrightness(now_ms);
}

void FirmwareApp::TogglePower(uint32_t now_ms) {
  LightState next = this->logical_light_;
  // A temporarily woken panel is visibly on even when its saved logical state
  // is off. The power button must turn that presentation off, not convert the
  // temporary wake into a permanent on state.
  next.on = this->overlay_queue_size_ != 0 ? false : !next.on;
  this->ApplyUserLight_(next, now_ms, true);
}

void FirmwareApp::StepBrightness(uint32_t now_ms) {
  if (!this->logical_light_.on && this->overlay_queue_size_ == 0)
    return;

  int next = this->brightness_step_ + this->brightness_direction_;
  if (next >= 3) {
    next = 3;
    this->brightness_direction_ = -1;
  } else if (next <= 0) {
    next = 0;
    this->brightness_direction_ = 1;
  }
  this->brightness_step_ = next;

  LightState light = this->logical_light_;
  light.brightness = static_cast<float>(next + 1) * 0.25f;
  this->ApplyUserLight_(light, now_ms, true);
}

void FirmwareApp::StopwatchStart(uint32_t now_ms) {
  this->AdvanceStopwatch_(now_ms);
  if (this->stopwatch_.elapsed_ms >= kStopwatchMaximumElapsedMs ||
      this->stopwatch_.running)
    return;
  this->stopwatch_.running = true;
  this->stopwatch_last_updated_ms_ = now_ms;
  this->RequestVisibleRender_();
}

void FirmwareApp::StopwatchStop(uint32_t now_ms) {
  this->AdvanceStopwatch_(now_ms);
  if (!this->stopwatch_.running)
    return;
  this->stopwatch_.running = false;
  this->RequestVisibleRender_();
}

void FirmwareApp::StopwatchReset(uint32_t now_ms) {
  this->AdvanceStopwatch_(now_ms);
  if (this->stopwatch_.elapsed_ms == 0 && !this->stopwatch_.running)
    return;
  this->stopwatch_ = {};
  this->stopwatch_last_updated_ms_ = now_ms;
  this->RequestVisibleRender_();
}

void FirmwareApp::TimerSet(uint32_t duration_ms, uint32_t now_ms) {
  this->StopTimerAlarm_();
  const uint32_t duration = std::min(duration_ms, kTimerMaximumDurationMs);
  if (this->timer_loaded_duration_ms_ == duration &&
      this->timer_.remaining_ms == duration && !this->timer_.running)
    return;
  this->timer_loaded_duration_ms_ = duration;
  this->timer_ = TimerSnapshot{duration, false};
  this->timer_last_updated_ms_ = now_ms;
  this->RequestVisibleRender_();
}

void FirmwareApp::TimerStart(uint32_t now_ms) {
  this->AdvanceTimer_(now_ms);
  this->StopTimerAlarm_();
  if (this->timer_.running || this->timer_.remaining_ms == 0)
    return;
  this->timer_.running = true;
  this->timer_last_updated_ms_ = now_ms;
  this->RequestVisibleRender_();
}

void FirmwareApp::TimerStop(uint32_t now_ms) {
  this->AdvanceTimer_(now_ms);
  this->StopTimerAlarm_();
  if (!this->timer_.running)
    return;
  this->timer_.running = false;
  this->RequestVisibleRender_();
}

void FirmwareApp::TimerReset(uint32_t now_ms) {
  this->AdvanceTimer_(now_ms);
  this->StopTimerAlarm_();
  if (this->timer_.remaining_ms == this->timer_loaded_duration_ms_ &&
      !this->timer_.running)
    return;
  this->timer_ = TimerSnapshot{this->timer_loaded_duration_ms_, false};
  this->timer_last_updated_ms_ = now_ms;
  this->RequestVisibleRender_();
}

void FirmwareApp::SelectDashboard(const std::string &dashboard_id) {
  DashboardSelection resolved = this->selected_dashboard_;
  if (!this->renderer_.ResolveDashboard(dashboard_id, &resolved))
    return;
  resolved.frame_interval_ms = ValidFrameInterval_(resolved.frame_interval_ms);
  this->selected_dashboard_ = std::move(resolved);
  this->base_rendered_ = false;
  this->base_render_requested_ = true;
  this->RequestVisibleRender_();
  this->ReconcileMicrophone_();
}

bool FirmwareApp::BeginFirmwareUpdate() {
  if (this->phase_ != Phase::kBootAnimation &&
      this->phase_ != Phase::kRunning)
    return false;

  const FrameView frame = this->renderer_.RenderFirmwareUpdate();
  // The update screen replaces the renderer's retained base pixels even when
  // presentation fails. Rebuild both the base and any active overlay next.
  this->base_render_requested_ = true;
  this->RequestVisibleRender_();
  return frame.valid() && this->panel_.Present(frame, true);
}

bool FirmwareApp::Notify(NotificationRequest request, uint32_t now_ms) {
  if (request.notification.text.overflowed() ||
      request.notification.title.overflowed())
    return false;
  OverlayRequest overlay_request;
  overlay_request.overlay.tag = OverlayTag::kNotification;
  overlay_request.overlay.notification = std::move(request.notification);
  overlay_request.requested_duration_ms = request.requested_duration_ms;
  overlay_request.has_sound = request.has_sound;
  overlay_request.sound = request.sound;
  return this->EnqueueOverlay_(std::move(overlay_request), now_ms);
}

bool FirmwareApp::React(Reaction reaction, uint32_t now_ms) {
  OverlayRequest request;
  request.overlay.tag = OverlayTag::kReaction;
  request.overlay.reaction = reaction;
  request.requested_duration_ms = ReactionVisibleDurationMs(reaction);
  request.has_sound = false;
  return this->EnqueueOverlay_(std::move(request), now_ms);
}

bool FirmwareApp::EnqueueOverlay_(OverlayRequest request, uint32_t now_ms) {
  if (this->overlay_queue_size_ == kOverlayQueueCapacity)
    return false;

  const bool first = this->overlay_queue_size_ == 0;
  const size_t tail =
      (this->overlay_queue_head_ + this->overlay_queue_size_) %
      kOverlayQueueCapacity;
  this->overlay_queue_storage_->slots[tail] = std::move(request);
  ++this->overlay_queue_size_;
  if (!first)
    return true;

  this->overlay_saved_light_ = this->logical_light_;
  this->ResetCurrentOverlayPresentation_();
  this->ReconcileMicrophone_();

  if (!this->logical_light_.on) {
    this->panel_.SetBrightness(1.0f);
    this->panel_.SetPower(true);
    this->BeginRepowerWaiting_(now_ms);
  } else if (this->phase_ == Phase::kOff) {
    this->panel_.SetBrightness(this->logical_light_.brightness);
    this->panel_.SetPower(true);
    this->BeginRepowerWaiting_(now_ms);
  }
  return true;
}

void FirmwareApp::ClearOverlayQueue() {
  if (this->overlay_queue_size_ == 0)
    return;
  this->RestoreOverlaySnapshot_();
}

void FirmwareApp::Reboot() {
  this->StopSound_();
  if (this->system_ != nullptr)
    this->system_->Reboot();
}

void FirmwareApp::FactoryReset() {
  this->StopSound_();
  if (this->system_ != nullptr)
    this->system_->FactoryReset();
}

const OverlayRequest &FirmwareApp::CurrentOverlayRequest_() const {
  return this->overlay_queue_storage_->slots[this->overlay_queue_head_];
}

const Overlay *FirmwareApp::current_overlay() const {
  if (this->overlay_queue_size_ == 0)
    return nullptr;
  return &this->CurrentOverlayRequest_().overlay;
}

bool FirmwareApp::notification_pending() const {
  const Overlay *overlay = this->current_overlay();
  return overlay != nullptr && overlay->tag == OverlayTag::kNotification &&
         !this->overlay_visible_;
}

bool FirmwareApp::notification_visible() const {
  const Overlay *overlay = this->current_overlay();
  return overlay != nullptr && overlay->tag == OverlayTag::kNotification &&
         this->overlay_visible_;
}

bool FirmwareApp::StopOverlaySound_() {
  if (!this->overlay_sound_started_)
    return false;
  const bool stopped = this->StopSoundIfOwned_(SoundOwner::kOverlay);
  this->overlay_sound_started_ = false;
  return stopped;
}

void FirmwareApp::ResetCurrentOverlayPresentation_() {
  this->overlay_visible_ = false;
  this->overlay_sound_started_ = false;
  this->overlay_visible_started_ms_ = 0;
  this->overlay_visible_duration_ms_ = 0;
  this->overlay_rendered_ = false;
  this->overlay_render_requested_ = true;
}

void FirmwareApp::ResetOverlayState_() {
  this->overlay_queue_storage_->slots = {};
  this->overlay_queue_head_ = 0;
  this->overlay_queue_size_ = 0;
  this->overlay_saved_light_ = {};
  this->ResetCurrentOverlayPresentation_();
  this->overlay_render_requested_ = false;
}

void FirmwareApp::PromoteOverlay_() {
  this->StopOverlaySound_();
  const bool previous_was_reaction =
      this->CurrentOverlayRequest_().overlay.tag == OverlayTag::kReaction;
  this->overlay_queue_storage_->slots[this->overlay_queue_head_] = {};
  this->overlay_queue_head_ =
      (this->overlay_queue_head_ + 1) % kOverlayQueueCapacity;
  --this->overlay_queue_size_;
  if (this->overlay_queue_size_ == 0)
    return;

  const OverlayTag next_tag = this->CurrentOverlayRequest_().overlay.tag;
  // A promoted reaction and a notification following a reaction must replace
  // the previous overlay artwork with a freshly rendered dashboard.
  if (this->overlay_saved_light_.on &&
      (next_tag == OverlayTag::kReaction ||
       (previous_was_reaction && next_tag == OverlayTag::kNotification)))
    this->base_render_requested_ = true;
  this->ResetCurrentOverlayPresentation_();
}

bool FirmwareApp::CancelOverlayQueueWithoutRestore_() {
  const bool overlay_sound_stopped = this->StopOverlaySound_();
  this->ResetOverlayState_();
  this->renderer_.ReleaseOverlayResources();
  this->ReconcileMicrophone_();
  return overlay_sound_stopped;
}

void FirmwareApp::RestoreOverlaySnapshot_() {
  const LightState restore = this->overlay_saved_light_;
  const bool overlay_sound_stopped = this->CancelOverlayQueueWithoutRestore_();
  this->logical_light_ = restore;
  this->panel_.SetBrightness(restore.brightness);

  if (!restore.on) {
    if (!overlay_sound_stopped && this->sound_owner_ != SoundOwner::kTimer)
      this->StopSound_();
    this->renderer_.HideBaseContent();
    this->panel_.SetPower(false);
    this->phase_ = Phase::kOff;
  } else {
    this->panel_.SetPower(true);
    this->base_render_requested_ = true;
  }
  this->ReconcileMicrophone_();
}

}  // namespace pixoo
