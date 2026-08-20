#pragma once

#include <atomic>
#include <cstring>
#include <functional>

#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "esp_heap_caps.h"

namespace esphome::pixoo64::adapters::async {

// A persistent background task that runs a job off the ESPHome main loop, so a
// blocking operation (e.g. an HTTP fetch + parse) does not stall the render
// tick.
//
// Construct with the job, call Start() once, then Wake() to request a run.
// Wake() returns immediately and coalesces: extra Wake()s while a run is
// pending or in progress ensure exactly one further run follows. The job runs
// on its configured FreeRTOS core.
//
// Start(), Wake(), RequestStop(), and Stop() are single-owner control methods;
// this adapter calls them only from ESPHome's main task. RequestStop() is
// non-blocking and asks the worker to leave at its next cooperative checkpoint.
// Stop() is the blocking destruction join and must run before captured job
// state is released. This class never force-deletes a running task. The job
// owns thread-safety of any data it produces (hand results back via
// SnapshotBuffer).
class AsyncWorker {
public:
  // core: FreeRTOS core id to pin to (default 1 = APP_CPU; Wi-Fi/lwIP run on
  // 0). stack_bytes: task stack; HTTPS(TLS) + JSON parsing needs headroom.
  // priority: must stay below the ESPHome main loop (priority 1).
  explicit AsyncWorker(std::function<void()> job, uint32_t stack_bytes = 12288,
                       UBaseType_t priority = 0, BaseType_t core = 1,
                       bool allow_internal_stack_fallback = true,
                       const char *task_name = "pixoo_async")
      : job_(std::move(job)), stack_bytes_(stack_bytes), priority_(priority),
        core_(core),
        allow_internal_stack_fallback_(allow_internal_stack_fallback) {
    if (task_name != nullptr) {
      std::strncpy(this->task_name_, task_name, sizeof(this->task_name_) - 1);
      this->task_name_[sizeof(this->task_name_) - 1] = '\0';
    }
  }

  ~AsyncWorker() { this->Stop(); }

  AsyncWorker(const AsyncWorker &) = delete;
  AsyncWorker &operator=(const AsyncWorker &) = delete;

  bool Start() {
    if (this->StopRequested())
      return false;
    if (this->task_ != nullptr)
      return true;

    // Both signals exist before the task starts and remain valid until Stop()
    // has received the worker's exit acknowledgement.
    this->wake_signal_ = xSemaphoreCreateBinary();
    if (this->wake_signal_ == nullptr)
      return false;
    this->stopped_signal_ = xSemaphoreCreateBinary();
    if (this->stopped_signal_ == nullptr) {
      vSemaphoreDelete(this->wake_signal_);
      this->wake_signal_ = nullptr;
      return false;
    }

    this->stopped_acknowledged_.store(false, std::memory_order_release);

    // Task creation happens-before the task runs, so TaskEntry observes
    // stack_in_psram_ without synchronization. Callers whose bulk work is not
    // latency-sensitive may require PSRAM rather than consuming scarce
    // internal RAM when external allocation fails.
    this->stack_in_psram_ =
        xTaskCreatePinnedToCoreWithCaps(
            &AsyncWorker::TaskEntry, this->task_name_, this->stack_bytes_, this,
            this->priority_, &this->task_, this->core_,
            MALLOC_CAP_SPIRAM) == pdPASS;
    if (!this->stack_in_psram_ &&
        (!this->allow_internal_stack_fallback_ ||
         xTaskCreatePinnedToCore(&AsyncWorker::TaskEntry, this->task_name_,
                                 this->stack_bytes_, this, this->priority_,
                                 &this->task_, this->core_) != pdPASS)) {
      this->task_ = nullptr;
      this->DeleteSignals_();
      this->stopped_acknowledged_.store(true, std::memory_order_release);
      return false;
    }
    return true;
  }

  // Request one run of the job. Non-blocking; safe to call every render tick.
  bool Wake() {
    if (this->task_ == nullptr || this->StopRequested() ||
        this->wake_signal_ == nullptr)
      return false;
    xSemaphoreGive(this->wake_signal_);
    return true;
  }

  // Non-blocking cooperative stop request. An active job must observe the stop
  // request itself; an idle task is woken so it can acknowledge exit.
  void RequestStop() {
    this->stopping_.store(true, std::memory_order_release);
    if (this->wake_signal_ != nullptr)
      xSemaphoreGive(this->wake_signal_);
  }

  bool StopRequested() const {
    return this->stopping_.load(std::memory_order_acquire);
  }

  // True before a task has started, after a failed start, or after the worker
  // has acknowledged its exit. It does not release worker resources.
  bool IsStopped() const {
    return this->stopped_acknowledged_.load(std::memory_order_acquire);
  }

  // Blocking destruction join. This must run before owned job state is
  // destroyed; it cannot impose a safe finite timeout on an active job.
  void Stop() {
    this->RequestStop();
    if (this->task_ == nullptr)
      return;

    xSemaphoreTake(this->stopped_signal_, portMAX_DELAY);
    this->task_ = nullptr;
    this->DeleteSignals_();
  }

private:
  static void TaskEntry(void *arg) {
    auto *self = static_cast<AsyncWorker *>(arg);
    for (;;) {
      // A binary semaphore clears on take, so multiple Wake()s between runs
      // collapse into a single further run.
      xSemaphoreTake(self->wake_signal_, portMAX_DELAY);
      if (self->stopping_.load(std::memory_order_acquire))
        break;
      if (self->job_)
        self->job_();
      if (self->stopping_.load(std::memory_order_acquire))
        break;
    }
    // A task created WithCaps must be deleted with the matching call.
    const bool with_caps = self->stack_in_psram_;
    self->stopped_acknowledged_.store(true, std::memory_order_release);
    xSemaphoreGive(self->stopped_signal_);
    if (with_caps)
      vTaskDeleteWithCaps(nullptr);
    else
      vTaskDelete(nullptr);
  }

  void DeleteSignals_() {
    if (this->wake_signal_ != nullptr) {
      vSemaphoreDelete(this->wake_signal_);
      this->wake_signal_ = nullptr;
    }
    if (this->stopped_signal_ != nullptr) {
      vSemaphoreDelete(this->stopped_signal_);
      this->stopped_signal_ = nullptr;
    }
  }

  std::function<void()> job_;
  bool stack_in_psram_{false};
  uint32_t stack_bytes_;
  UBaseType_t priority_;
  BaseType_t core_;
  bool allow_internal_stack_fallback_;
  char task_name_[configMAX_TASK_NAME_LEN]{"pixoo_async"};
  std::atomic<bool> stopping_{false};
  std::atomic<bool> stopped_acknowledged_{true};
  TaskHandle_t task_{nullptr};
  SemaphoreHandle_t wake_signal_{nullptr};
  SemaphoreHandle_t stopped_signal_{nullptr};
};

} // namespace esphome::pixoo64::adapters::async
