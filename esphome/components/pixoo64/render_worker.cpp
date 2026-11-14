#include "render_worker.h"

#include <cstdlib>
#include <new>
#include <utility>

#include "esp_heap_caps.h"
#include "esp_system.h"
#include "esphome/core/hal.h"
#include "esphome/core/log.h"
#include "pixoo64.h"

namespace esphome::pixoo64 {
namespace {
constexpr char TAG[] = "pixoo64.render_worker";
constexpr uint32_t kStartupTimeoutMs = 2000;
constexpr uint32_t kStopTimeoutMs = 2000;
constexpr uint32_t kServiceIntervalMs = 5;
constexpr unsigned kCommandsPerPass = 32;
constexpr uint32_t kStackBytes = 8 * 1024;

int StateIndex(WorkerCommandKind kind) {
  switch (kind) {
    case WorkerCommandKind::kDashboard: return 0;
    case WorkerCommandKind::kLight: return 1;
    case WorkerCommandKind::kSolarEnabled: return 2;
    case WorkerCommandKind::kSolarLevels: return 3;
    case WorkerCommandKind::kSolarElevation: return 4;
    default: return -1;
  }
}
bool IsButton(WorkerCommandKind kind) {
  return kind == WorkerCommandKind::kPowerPressed || kind == WorkerCommandKind::kPowerReleased ||
         kind == WorkerCommandKind::kBrightnessPressed || kind == WorkerCommandKind::kBrightnessReleased;
}
TickType_t WaitTicks(uint32_t ms) {
  const TickType_t ticks = pdMS_TO_TICKS(ms);
  return ticks == 0 ? 1 : ticks;
}
}  // namespace

RenderWorker::RenderWorker(Pixoo64Panel &panel, pixoo::RenderPort &renderer, uint32_t metrics_window_ms)
    : panel_(panel), renderer_(renderer), metrics_window_ms_(metrics_window_ms),
      queue_storage_(AllocateQueueStorage_()),
      commands_(queue_storage_ ? &queue_storage_->commands : nullptr),
      buttons_(queue_storage_ ? &queue_storage_->buttons : nullptr),
      outputs_(queue_storage_ ? &queue_storage_->outputs : nullptr) {}

RenderWorker::QueueStorage *RenderWorker::AllocateQueueStorage_() {
  void *memory = heap_caps_malloc(sizeof(QueueStorage), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  return memory ? new (memory) QueueStorage{} : nullptr;
}
void *RenderWorker::operator new(size_t size, const std::nothrow_t &) noexcept {
  return heap_caps_malloc(size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
}
void RenderWorker::operator delete(void *ptr) noexcept { heap_caps_free(ptr); }
void RenderWorker::operator delete(void *ptr, const std::nothrow_t &) noexcept { heap_caps_free(ptr); }

RenderWorker::~RenderWorker() {
  RequestStop();
  const uint32_t started = millis();
  while (!IsStopped() && millis() - started < kStopTimeoutMs) vTaskDelay(WaitTicks(1));
  if (!IsStopped()) {
    ESP_LOGE(TAG, "worker did not stop; restarting to protect its resources");
    esp_restart();
    std::abort();
  }
  if (queue_storage_) {
    queue_storage_->~QueueStorage();
    heap_caps_free(queue_storage_);
  }
}

bool RenderWorker::Start(pixoo::LightState initial, const std::string &dashboard,
                         pixoo::SolarBrightnessConfig solar) {
  if (start_attempted_) return false;
  start_attempted_ = true;
  if (!queue_storage_) return false;
  initial_light_ = initial;
  initial_dashboard_ = dashboard;
  initial_solar_ = solar;
  stopped_.store(false, std::memory_order_release);
  if (xTaskCreatePinnedToCore(TaskEntry_, "pixoo_render", kStackBytes, this, 1, nullptr, 1) != pdPASS) {
    stopped_.store(true, std::memory_order_release);
    return false;
  }
  const uint32_t started = millis();
  while (startup_.load(std::memory_order_acquire) == 0 && millis() - started < kStartupTimeoutMs)
    vTaskDelay(WaitTicks(1));
  if (startup_.load(std::memory_order_acquire) != 1) {
    RequestStop();
    return false;
  }
  return true;
}

bool RenderWorker::Submit(WorkerCommand &&command) {
  if (startup_.load(std::memory_order_acquire) != 1 ||
      stop_requested_.load(std::memory_order_acquire) || IsStopped()) return false;
  const int index = StateIndex(command.kind);
  if (index >= 0) {
    std::lock_guard<std::mutex> guard(states_mutex_);
    queue_storage_->states[index] = std::move(command);
  } else if (IsButton(command.kind)) {
    if (!buttons_.TryPush(std::move(command))) {
      ESP_LOGE(TAG, "button edge queue full; restarting to clear button state");
      RequestStop();
      esp_restart();
      std::abort();
    }
  } else if (!commands_.TryPush(std::move(command))) {
    return false;
  }
  Wake_();
  return true;
}

bool RenderWorker::RequestReboot() {
  portENTER_CRITICAL(&lock_);
  const bool available = startup_.load(std::memory_order_acquire) == 1 && task_ &&
                         !stop_requested_.load(std::memory_order_acquire) &&
                         !stopped_.load(std::memory_order_acquire);
  if (available) {
    reboot_requested_.store(true, std::memory_order_release);
    xTaskNotifyGive(task_);
  }
  portEXIT_CRITICAL(&lock_);
  return available;
}

bool RenderWorker::RequestFirmwareUpdate(uint32_t ticket) {
  if (startup_.load(std::memory_order_acquire) != 1 ||
      stop_requested_.load(std::memory_order_acquire) || IsStopped() ||
      update_pending_.load(std::memory_order_acquire)) return false;
  update_ticket_.store(ticket, std::memory_order_relaxed);
  update_pending_.store(true, std::memory_order_release);
  Wake_();
  return true;
}
void RenderWorker::ResumeFirmwareUpdate() {
  resume_requested_.store(true, std::memory_order_release);
  Wake_();
}

bool RenderWorker::PopNext_(WorkerCommand &command) {
  std::lock_guard<std::mutex> guard(states_mutex_);
  const WorkerCommand *best = commands_.Peek();
  int source = best ? -1 : -3;
  const WorkerCommand *button = buttons_.Peek();
  if (button && (!best || WorkerSequenceBefore(button->sequence, best->sequence))) {
    best = button;
    source = -2;
  }
  auto &states = queue_storage_->states;
  for (size_t i = 0; i < states.size(); ++i) {
    if (states[i] && (!best || WorkerSequenceBefore(states[i]->sequence, best->sequence))) {
      best = &*states[i];
      source = static_cast<int>(i);
    }
  }
  if (source == -3) return false;
  if (source == -1) return commands_.TryPop(command);
  if (source == -2) return buttons_.TryPop(command);
  command = std::move(*states[source]);
  states[source].reset();
  return true;
}
bool RenderWorker::ReadOutput(WorkerOutput *output) {
  if (!output) return false;
  portENTER_CRITICAL(&lock_);
  const bool has_system_output = system_output_.has_value();
  if (has_system_output) {
    *output = *system_output_;
    system_output_.reset();
  }
  portEXIT_CRITICAL(&lock_);
  return has_system_output || outputs_.TryPop(*output);
}
WorkerState RenderWorker::State() const {
  portENTER_CRITICAL(&lock_);
  const WorkerState copy = state_;
  portEXIT_CRITICAL(&lock_);
  return copy;
}
void RenderWorker::Wake_() {
  portENTER_CRITICAL(&lock_);
  if (task_) xTaskNotifyGive(task_);
  portEXIT_CRITICAL(&lock_);
}
void RenderWorker::RequestStop() {
  stop_requested_.store(true, std::memory_order_release);
  Wake_();
}
bool RenderWorker::IsStopped() const { return stopped_.load(std::memory_order_acquire); }
void RenderWorker::TaskEntry_(void *arg) {
  static_cast<RenderWorker *>(arg)->Run_();
  vTaskDelete(nullptr);
}

void RenderWorker::Run_() {
  portENTER_CRITICAL(&lock_);
  task_ = xTaskGetCurrentTaskHandle();
  portEXIT_CRITICAL(&lock_);
  bool started = false;
  if (!stop_requested_.load(std::memory_order_acquire) && panel_.WorkerSetup()) {
    void *memory = heap_caps_malloc(sizeof(pixoo::OverlayQueueStorage), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!memory) memory = heap_caps_malloc(sizeof(pixoo::OverlayQueueStorage), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (memory) {
      overlay_storage_ = new (memory) pixoo::OverlayQueueStorage{};
      app_.emplace(panel_, renderer_, this, this, this, pixoo::FirmwareAppConfig{},
                   this, this, overlay_storage_, this);
      const uint32_t now = millis();
      if (metrics_window_ms_) metrics_window_.Reset(now);
      started = app_->Start(now, initial_light_, initial_dashboard_, initial_solar_);
      if (started) PublishState_(millis());
    } else {
      ESP_LOGE(TAG, "overlay queue storage allocation failed");
    }
  }
  initial_dashboard_.clear();
  if (started && !stop_requested_.load(std::memory_order_acquire)) {
    startup_.store(1, std::memory_order_release);
    uint32_t last_tick = millis() - kServiceIntervalMs;
    while (!stop_requested_.load(std::memory_order_acquire)) {
      WorkerCommand command;
      if (reboot_requested_.exchange(false, std::memory_order_acq_rel)) app_->Reboot();
      if (resume_requested_.exchange(false, std::memory_order_acq_rel)) update_paused_ = false;
      unsigned command_count = 0;
      if (!update_paused_) {
        for (; command_count < kCommandsPerPass &&
                                 !stop_requested_.load(std::memory_order_acquire) &&
                                 !update_pending_.load(std::memory_order_acquire) &&
                                 !reboot_requested_.load(std::memory_order_acquire) &&
                                 PopNext_(command); ++command_count) {
          Apply_(command);
          command = WorkerCommand{};
        }
      }
      if (reboot_requested_.load(std::memory_order_acquire)) continue;
      if (!stop_requested_.load(std::memory_order_acquire) && update_pending_.load(std::memory_order_acquire)) {
        const uint32_t ticket = update_ticket_.load(std::memory_order_relaxed);
        update_pending_.store(false, std::memory_order_release);
        const bool succeeded = app_->BeginFirmwareUpdate(millis());
        update_paused_ = true;
        portENTER_CRITICAL(&lock_);
        state_.update_completed_sequence = ticket;
        state_.update_succeeded = succeeded;
        portEXIT_CRITICAL(&lock_);
      }
      const uint32_t now = millis();
      if (now - last_tick >= kServiceIntervalMs) {
        last_tick = now;
        if (!update_paused_) app_->Tick(now);
        panel_.WorkerLoop();
        PublishState_(millis());
      }
      if (!stop_requested_.load(std::memory_order_acquire)) {
        const uint32_t elapsed = millis() - last_tick;
        if (elapsed < kServiceIntervalMs && command_count < kCommandsPerPass)
          ulTaskNotifyTake(pdTRUE, WaitTicks(kServiceIntervalMs - elapsed));
        else
          vTaskDelay(WaitTicks(1));
      }
    }
  }
  WorkerCommand discarded;
  while (queue_storage_ && PopNext_(discarded)) discarded = WorkerCommand{};
  if (app_) {
    renderer_.HideBaseContent(millis());
    renderer_.ReleaseOverlayResources();
    app_.reset();
  }
  panel_.WorkerShutdown();
  if (overlay_storage_) {
    overlay_storage_->~OverlayQueueStorage();
    heap_caps_free(overlay_storage_);
    overlay_storage_ = nullptr;
  }
  if (!started || startup_.load(std::memory_order_relaxed) == 0) startup_.store(2, std::memory_order_release);
  portENTER_CRITICAL(&lock_);
  task_ = nullptr;
  portEXIT_CRITICAL(&lock_);
  stopped_.store(true, std::memory_order_release);
}

void RenderWorker::Apply_(WorkerCommand &c) {
  applied_sequence_ = c.sequence;
  const uint32_t t = millis();
  switch (c.kind) {
    case WorkerCommandKind::kDashboard: app_->SelectDashboard(c.dashboard); break;
    case WorkerCommandKind::kLight: app_->SetUserLight(c.light, t); break;
    case WorkerCommandKind::kSolarEnabled: app_->SetSolarBrightnessEnabled(c.enabled, t); break;
    case WorkerCommandKind::kSolarLevels: app_->SetSolarBrightnessLevels(c.value, c.second_value, t); break;
    case WorkerCommandKind::kSolarElevation: app_->SetSolarElevation(c.value, t); break;
    case WorkerCommandKind::kPowerPressed: app_->PowerButtonPressed(c.timestamp_ms); break;
    case WorkerCommandKind::kPowerReleased: app_->PowerButtonReleased(c.timestamp_ms, t); break;
    case WorkerCommandKind::kBrightnessPressed: app_->BrightnessButtonPressed(c.timestamp_ms); break;
    case WorkerCommandKind::kBrightnessReleased: app_->BrightnessButtonReleased(c.timestamp_ms, t); break;
    case WorkerCommandKind::kNotification:
      if (!c.notification || !app_->Notify(std::move(*c.notification), t)) ESP_LOGW(TAG, "notification rejected");
      break;
    case WorkerCommandKind::kReaction: if (!app_->React(c.reaction, t)) ESP_LOGW(TAG, "reaction rejected"); break;
    case WorkerCommandKind::kClearOverlays: app_->ClearOverlayQueue(t); break;
    case WorkerCommandKind::kStopwatchStart: app_->StopwatchStart(t); break;
    case WorkerCommandKind::kTimerStart: if (!app_->TimerStart(c.integer, t)) ESP_LOGW(TAG, "timer rejected"); break;
    case WorkerCommandKind::kTimingPause: app_->TimingPause(t); break;
    case WorkerCommandKind::kTimingResume: app_->TimingResume(t); break;
    case WorkerCommandKind::kTimingStop: app_->TimingStop(t); break;
    case WorkerCommandKind::kTimingMinimize: app_->TimingMinimize(t); break;
    case WorkerCommandKind::kTimingShow: app_->TimingShow(t); break;
    case WorkerCommandKind::kReboot: app_->Reboot(); break;
  }
  PublishState_(millis());
}

void RenderWorker::PublishState_(uint32_t now_ms) {
  pixoo::FrameMetricsSnapshot snapshot;
  const bool closed = metrics_window_ms_ && metrics_window_.IsDue(now_ms, metrics_window_ms_) &&
                      metrics_window_.Close(now_ms, &snapshot);
  const auto timing = app_->timing();
  const bool solar_enabled = app_->solar_brightness_enabled();
  const bool solar_due = app_->SolarElevationDue(now_ms);
  portENTER_CRITICAL(&lock_);
  state_.timing = timing;
  state_.solar_enabled = solar_enabled;
  state_.solar_due = solar_due;
  state_.processed_sequence = applied_sequence_;
  if (closed) { state_.metrics = snapshot; ++state_.metrics_generation; }
  portEXIT_CRITICAL(&lock_);
}
void RenderWorker::Emit_(WorkerOutput output) {
  output.sequence = applied_sequence_;
  if (output.kind == WorkerOutputKind::kReboot || output.kind == WorkerOutputKind::kFactoryReset) {
    portENTER_CRITICAL(&lock_);
    if (!system_output_ || system_output_->kind != WorkerOutputKind::kFactoryReset)
      system_output_ = output;
    portEXIT_CRITICAL(&lock_);
    return;
  }
  while (!stop_requested_.load(std::memory_order_acquire)) {
    if (outputs_.TryPush(std::move(output))) return;
    vTaskDelay(WaitTicks(1));
  }
}
void RenderWorker::Play(pixoo::Sound sound) { WorkerOutput o; o.kind = WorkerOutputKind::kSoundPlay; o.sound = sound; Emit_(o); }
void RenderWorker::Stop() { WorkerOutput o; o.kind = WorkerOutputKind::kSoundStop; Emit_(o); }
void RenderWorker::SetEnabled(bool enabled) { WorkerOutput o; o.kind = WorkerOutputKind::kMicrophoneEnabled; o.flag = enabled; Emit_(o); }
void RenderWorker::Publish(pixoo::LightState state, bool persistent) { WorkerOutput o; o.kind = WorkerOutputKind::kLight; o.light = state; o.flag = persistent; Emit_(o); }
void RenderWorker::PublishSolarBrightnessEnabled(bool enabled) { WorkerOutput o; o.kind = WorkerOutputKind::kSolarEnabled; o.flag = enabled; Emit_(o); }
void RenderWorker::Reboot() { WorkerOutput o; o.kind = WorkerOutputKind::kReboot; Emit_(o); }
void RenderWorker::FactoryReset() { WorkerOutput o; o.kind = WorkerOutputKind::kFactoryReset; Emit_(o); }
void RenderWorker::BeginRegularFrame() {
  if (metrics_window_ms_) { frame_started_us_ = micros(); frame_active_ = true; }
}
void RenderWorker::EndRegularFrame() {
  if (frame_active_) { metrics_window_.Record(micros() - frame_started_us_); frame_active_ = false; }
}

}  // namespace esphome::pixoo64
