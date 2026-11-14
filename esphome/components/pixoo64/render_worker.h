#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <new>
#include <optional>
#include <string>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "render_worker_bridge.h"

namespace esphome::pixoo64 {

class Pixoo64Panel;

// Control requests, Submit and ReadOutput are called on the main task.
class RenderWorker final : public pixoo::SoundPlayer,
                           public pixoo::MicrophonePort,
                           public pixoo::LightStateSink,
                           public pixoo::SolarBrightnessStateSink,
                           public pixoo::SystemPort,
                           public pixoo::FrameMetricsPort {
 public:
  RenderWorker(Pixoo64Panel &panel, pixoo::RenderPort &renderer, uint32_t metrics_window_ms);
  ~RenderWorker() override;
  static void *operator new(size_t size, const std::nothrow_t &) noexcept;
  static void operator delete(void *ptr) noexcept;
  static void operator delete(void *ptr, const std::nothrow_t &) noexcept;
  RenderWorker(const RenderWorker &) = delete;
  RenderWorker &operator=(const RenderWorker &) = delete;

  bool Start(pixoo::LightState initial, const std::string &dashboard, pixoo::SolarBrightnessConfig solar);
  bool Submit(WorkerCommand &&command);
  bool RequestReboot();
  bool RequestFirmwareUpdate(uint32_t ticket);
  void ResumeFirmwareUpdate();
  bool ReadOutput(WorkerOutput *output);
  WorkerState State() const;
  void RequestStop();
  bool IsStopped() const;

  void Play(pixoo::Sound sound) override;
  void Stop() override;
  void SetEnabled(bool enabled) override;
  void Publish(pixoo::LightState state, bool persistent) override;
  void PublishSolarBrightnessEnabled(bool enabled) override;
  void Reboot() override;
  void FactoryReset() override;
  void BeginRegularFrame() override;
  void EndRegularFrame() override;

 private:
  struct QueueStorage {
    WorkerCommandQueue::Storage commands;
    WorkerButtonQueue::Storage buttons;
    WorkerOutputQueue::Storage outputs;
    std::array<std::optional<WorkerCommand>, 5> states{};
  };
  static QueueStorage *AllocateQueueStorage_();
  static void TaskEntry_(void *arg);
  void Run_();
  void Apply_(WorkerCommand &command);
  bool PopNext_(WorkerCommand &command);
  void PublishState_(uint32_t now_ms);
  void Emit_(WorkerOutput output);
  void Wake_();

  Pixoo64Panel &panel_;
  pixoo::RenderPort &renderer_;
  const uint32_t metrics_window_ms_;
  QueueStorage *queue_storage_;
  WorkerCommandQueue commands_;
  WorkerButtonQueue buttons_;
  std::mutex states_mutex_;
  WorkerOutputQueue outputs_;
  std::atomic<uint32_t> update_ticket_{0};
  std::atomic<bool> update_pending_{false};
  std::atomic<bool> reboot_requested_{false};
  std::atomic<bool> resume_requested_{false};
  mutable portMUX_TYPE lock_ = portMUX_INITIALIZER_UNLOCKED;
  WorkerState state_{};
  std::optional<WorkerOutput> system_output_;
  TaskHandle_t task_{nullptr};
  std::atomic<bool> stop_requested_{false};
  std::atomic<bool> stopped_{true};
  std::atomic<uint8_t> startup_{0};
  bool start_attempted_{false};
  pixoo::LightState initial_light_{};
  pixoo::SolarBrightnessConfig initial_solar_{};
  std::string initial_dashboard_;

  // Worker-owned until stopped_ is published.
  pixoo::OverlayQueueStorage *overlay_storage_{nullptr};
  std::optional<pixoo::FirmwareApp> app_;
  pixoo::FrameMetricsWindow metrics_window_;
  uint32_t frame_started_us_{0};
  uint32_t applied_sequence_{0};
  bool frame_active_{false};
  bool update_paused_{false};
};

}  // namespace esphome::pixoo64
