#pragma once

#include <cstdint>
#include <cstdlib>
#include <memory>
#include <string>
#include <type_traits>

#include "firmware_app.h"
#include "render_worker_queue.h"

namespace esphome::pixoo64 {

enum class WorkerCommandKind : uint8_t {
  kDashboard, kLight, kSolarEnabled, kSolarLevels, kSolarElevation,
  kPowerPressed, kPowerReleased, kBrightnessPressed, kBrightnessReleased,
  kNotification, kReaction, kClearOverlays,
  kStopwatchStart, kTimerStart, kTimingPause, kTimingResume, kTimingStop,
  kTimingMinimize, kTimingShow, kReboot,
};

struct PendingNotificationDeleter {
  void operator()(pixoo::NotificationRequest *request) const noexcept {
    if (request) {
      request->~NotificationRequest();
      std::free(request);
    }
  }
};

struct WorkerCommand {
  WorkerCommandKind kind{WorkerCommandKind::kDashboard};
  uint32_t timestamp_ms{0};
  uint32_t sequence{0};
  pixoo::LightState light{};
  std::unique_ptr<pixoo::NotificationRequest, PendingNotificationDeleter> notification;
  pixoo::Reaction reaction{pixoo::Reaction::kLaughing};
  std::string dashboard;
  float value{0};
  float second_value{0};
  int32_t integer{0};
  bool enabled{false};
};

enum class WorkerOutputKind : uint8_t {
  kLight, kSolarEnabled, kSoundPlay, kSoundStop, kMicrophoneEnabled,
  kReboot, kFactoryReset,
};

struct WorkerOutput {
  WorkerOutputKind kind{WorkerOutputKind::kSoundStop};
  uint32_t sequence{0};
  pixoo::LightState light{};
  pixoo::Sound sound{pixoo::Sound::kChirp};
  bool flag{false};
};
static_assert(std::is_trivially_copyable<WorkerOutput>::value,
              "worker outputs must not own dynamic storage");

struct WorkerState {
  pixoo::TimingSnapshot timing{};
  pixoo::FrameMetricsSnapshot metrics{};
  uint32_t processed_sequence{0};
  uint32_t metrics_generation{0};
  uint32_t update_completed_sequence{0};
  bool update_succeeded{false};
  bool solar_enabled{false};
  bool solar_due{false};
};
static_assert(std::is_trivially_copyable<WorkerState>::value,
              "worker state must not own dynamic storage");

// Valid when fewer than 2^31 commands are outstanding.
inline bool WorkerSequenceBefore(uint32_t sequence, uint32_t reference) {
  return static_cast<int32_t>(sequence - reference) < 0;
}

using WorkerCommandQueue = RenderWorkerQueue<WorkerCommand, 32>;
using WorkerButtonQueue = RenderWorkerQueue<WorkerCommand, 16>;
using WorkerOutputQueue = RenderWorkerQueue<WorkerOutput, 32>;

}  // namespace esphome::pixoo64
