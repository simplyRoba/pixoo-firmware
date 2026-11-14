#include <atomic>
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <iostream>
#include <mutex>
#include <thread>
#include <vector>

#include "esp_heap_caps.h"
#include "esphome/core/hal.h"
#include "pixoo64.h"
#include "render_worker.h"

using namespace std::chrono_literals;
using namespace esphome::pixoo64;

bool fail_spiram_allocation = false;

namespace {
struct Task {
  std::mutex mutex;
  std::condition_variable cv;
  unsigned notifications = 0;
  std::thread thread;
};
thread_local Task *current_task = nullptr;
std::vector<Task *> tasks;
const auto epoch = std::chrono::steady_clock::now();
std::atomic<uint32_t> clock_offset{0};
bool fail_task_creation = false;

pixoo::NotificationRequest *make_notification() {
  void *memory = std::malloc(sizeof(pixoo::NotificationRequest));
  assert(memory != nullptr);
  return new (memory) pixoo::NotificationRequest{};
}

struct Probe {
  std::mutex mutex;
  std::condition_variable cv;
  bool setup_ok = true;
  bool resolve_ok = true;
  bool block = false;
  bool entered = false;
  bool released = false;
  int setup = 0, shutdown = 0, content = 0, present = 0, forced = 0;
  int overlays = 0, loops = 0;
  const uint8_t *buffer = nullptr;
  TaskHandle_t owner = nullptr;
  std::string dashboard;
} *probe;

void worker_only() {
  assert(current_task != nullptr);
  assert(xPortGetCoreID() == 1);
  if (probe->owner) assert(xTaskGetCurrentTaskHandle() == probe->owner);
}
void signal() { probe->cv.notify_all(); }
template<class Predicate> void wait_for(Predicate predicate) {
  std::unique_lock<std::mutex> lock(probe->mutex);
  if (!probe->cv.wait_for(lock, 3s, predicate)) {
    std::cerr << "timed out waiting for worker event: content=" << probe->content
              << " present=" << probe->present << " overlays=" << probe->overlays
              << " loops=" << probe->loops << " entered=" << probe->entered
              << " dashboard=" << probe->dashboard << "\n";
    std::abort();
  }
}
template<class Predicate> void poll(Predicate predicate) {
  auto deadline = std::chrono::steady_clock::now() + 3s;
  while (!predicate()) {
    if (std::chrono::steady_clock::now() >= deadline) {
      std::cerr << "timed out polling worker state\n";
      std::abort();
    }
    std::this_thread::sleep_for(1ms);
  }
}
void join_tasks() {
  for (auto *task : tasks) { task->thread.join(); delete task; }
  tasks.clear();
}
void advance(uint32_t ms) { clock_offset.fetch_add(ms); }

class Renderer final : public pixoo::RenderPort {
 public:
  uint8_t pixels[64]{};
  bool ResolveDashboard(const std::string &id, pixoo::DashboardSelection *out) override {
    worker_only();
    if (!probe->resolve_ok) return false;
    *out = {id, id == "mic", 5};
    return true;
  }
  pixoo::FrameView RenderBootAnimation(uint32_t) override { worker_only(); return frame(); }
  pixoo::FrameView RenderFirmwareUpdate() override { worker_only(); return frame(); }
  uint32_t NotificationMinVisibleMs(const pixoo::Notification &) override { worker_only(); return 1; }
  void HideBaseContent(uint32_t) override { worker_only(); }
  void ReleaseOverlayResources() override {
    worker_only(); std::lock_guard<std::mutex> lock(probe->mutex); probe->released = true; signal();
  }
  bool RenderContent(uint32_t, const std::string &id, const pixoo::TimingSnapshot &,
                     const pixoo::Overlay *overlay, uint32_t, bool, bool, bool, bool,
                     pixoo::FrameView *out) override {
    worker_only();
    std::unique_lock<std::mutex> lock(probe->mutex);
    ++probe->content; probe->dashboard = id;
    if (overlay) ++probe->overlays;
    if (probe->block) {
      probe->entered = true; signal();
      probe->cv.wait(lock, [] { return !probe->block; });
    }
    *out = frame(); signal(); return true;
  }
  pixoo::FrameView frame() { return {pixels, sizeof(pixels)}; }
};

void start_running(RenderWorker &worker) {
  assert(worker.Start({}, "default", {}));
  advance(1100);
  wait_for([] { return probe->present > 0; }); // boot frame
  advance(5100);
  wait_for([] { return probe->content > 0; });
}
void submit(RenderWorker &worker, WorkerCommand command) {
  command.timestamp_ms = esphome::millis();
  assert(worker.Submit(std::move(command)));
}
void processed(RenderWorker &worker, uint32_t sequence) {
  poll([&] { return worker.State().processed_sequence == sequence; });
}
void drain(RenderWorker &worker, std::vector<WorkerOutput> &outputs) {
  WorkerOutput output;
  while (worker.ReadOutput(&output)) outputs.push_back(output);
}
void block_next(RenderWorker &worker, uint32_t sequence) {
  { std::lock_guard<std::mutex> lock(probe->mutex); probe->block = true; probe->entered = false; }
  WorkerCommand cmd; cmd.kind = WorkerCommandKind::kDashboard; cmd.dashboard = "blocked"; cmd.sequence = sequence;
  submit(worker, std::move(cmd));
  wait_for([] { return probe->entered; });
}
void unblock() {
  std::lock_guard<std::mutex> lock(probe->mutex); probe->block = false; signal();
}

void test_commands_and_backpressure() {
  Probe p; probe = &p; Renderer renderer; p.buffer = renderer.pixels;
  {
    Pixoo64Panel panel; RenderWorker worker(panel, renderer, 0);
    start_running(worker);
    WorkerCommand light; light.kind = WorkerCommandKind::kLight; light.light = {true, .4f}; light.sequence = 1; submit(worker, std::move(light));
    WorkerCommand dash; dash.kind = WorkerCommandKind::kDashboard; dash.dashboard = "mic"; dash.sequence = 2; submit(worker, std::move(dash));
    WorkerCommand note; note.kind = WorkerCommandKind::kNotification; note.notification.reset(make_notification()); note.notification->notification.text = "hello"; note.notification->has_sound = true; note.sequence = 3; submit(worker, std::move(note));
    processed(worker, 3);
    wait_for([] { return probe->overlays > 0; });
    WorkerCommand solar_on; solar_on.kind = WorkerCommandKind::kSolarEnabled; solar_on.enabled = true; solar_on.sequence = 4; submit(worker, std::move(solar_on));
    WorkerCommand solar_off; solar_off.kind = WorkerCommandKind::kSolarEnabled; solar_off.enabled = false; solar_off.sequence = 5; submit(worker, std::move(solar_off));
    WorkerCommand timer; timer.kind = WorkerCommandKind::kTimerStart; timer.integer = 30; timer.sequence = 6; submit(worker, std::move(timer));
    processed(worker, 6);
    assert(worker.State().timing.kind == pixoo::TimingKind::kTimer);
    std::vector<WorkerOutput> outputs; drain(worker, outputs);
    bool mic = false, sound = false;
    for (auto &out : outputs) { mic |= out.kind == WorkerOutputKind::kMicrophoneEnabled && out.flag; sound |= out.kind == WorkerOutputKind::kSoundPlay; }
    assert(mic && sound);

    block_next(worker, 7);
    auto before = std::chrono::steady_clock::now();
    for (uint32_t i = 0; i < 32; ++i) {
      WorkerCommand c; c.kind = WorkerCommandKind::kClearOverlays; c.sequence = 8 + i;
      submit(worker, std::move(c));
      (void) worker.State(); drain(worker, outputs);
    }
    WorkerCommand rejected; rejected.kind = WorkerCommandKind::kNotification; rejected.notification.reset(make_notification()); rejected.notification->notification.text = "retained"; rejected.sequence = 40;
    assert(!worker.Submit(std::move(rejected)));
    assert(rejected.notification && std::string(rejected.notification->notification.text.c_str()) == "retained" && rejected.sequence == 40);
    assert(std::chrono::steady_clock::now() - before < 1s);
    unblock(); processed(worker, 39);
    worker.RequestStop(); poll([&] { return worker.IsStopped(); });
  }
  join_tasks(); assert(p.shutdown == 1 && p.released);
}

void test_delayed_commands_and_coalescing() {
  Probe p; probe = &p; Renderer renderer; p.buffer = renderer.pixels;
  {
    Pixoo64Panel panel; RenderWorker worker(panel, renderer, 0); start_running(worker);
    block_next(worker, 1);
    for (uint32_t i = 0; i < 31; ++i) {
      WorkerCommand c; c.kind = WorkerCommandKind::kClearOverlays; c.sequence = 2 + i;
      submit(worker, std::move(c));
    }
    WorkerCommand pressed; pressed.kind = WorkerCommandKind::kPowerPressed; pressed.sequence = 34;
    submit(worker, std::move(pressed));
    advance(60);
    WorkerCommand released; released.kind = WorkerCommandKind::kPowerReleased; released.sequence = 35;
    submit(worker, std::move(released));
    for (uint32_t i = 0; i < 100; ++i) {
      WorkerCommand c; c.kind = WorkerCommandKind::kDashboard; c.dashboard = "latest-" + std::to_string(i); c.sequence = 36 + i;
      submit(worker, std::move(c));
    }
    WorkerCommand timer; timer.kind = WorkerCommandKind::kTimerStart; timer.integer = 10; timer.sequence = 136;
    submit(worker, std::move(timer));
    assert(worker.RequestFirmwareUpdate(123));
    advance(5000);
    unblock();
    poll([&] { return worker.State().update_completed_sequence == 123; });
    assert(worker.State().processed_sequence == 1);
    worker.ResumeFirmwareUpdate();
    processed(worker, 136);
    assert(worker.State().timing.kind == pixoo::TimingKind::kTimer);
    assert(worker.State().timing.value_ms > 9000);
    std::vector<WorkerOutput> outputs; drain(worker, outputs);
    bool switched_off = false;
    for (const auto &out : outputs) switched_off |= out.kind == WorkerOutputKind::kLight && !out.light.on;
    assert(switched_off);
    worker.RequestStop(); poll([&] { return worker.IsStopped(); });
  }
  join_tasks();
}

void test_system_output_latch() {
  Probe p; probe = &p; Renderer renderer; p.buffer = renderer.pixels;
  {
    Pixoo64Panel panel; RenderWorker worker(panel, renderer, 0); start_running(worker);
    block_next(worker, 1);
    std::vector<WorkerOutput> initial; drain(worker, initial);
    for (int i = 0; i < 32; ++i) worker.Play(pixoo::Sound::kChirp);
    worker.Reboot();
    worker.FactoryReset();
    worker.Reboot();
    WorkerOutput output;
    assert(worker.ReadOutput(&output) && output.kind == WorkerOutputKind::kFactoryReset);
    for (int i = 0; i < 32; ++i)
      assert(worker.ReadOutput(&output) && output.kind == WorkerOutputKind::kSoundPlay);
    assert(!worker.ReadOutput(&output));
    unblock(); worker.RequestStop(); poll([&] { return worker.IsStopped(); });
  }
  join_tasks();
}

void test_flood_does_not_starve_frames_or_update() {
  Probe p; probe = &p; Renderer renderer; p.buffer = renderer.pixels;
  {
    Pixoo64Panel panel; RenderWorker worker(panel, renderer, 0); start_running(worker);
    int frames;
    { std::lock_guard<std::mutex> lock(p.mutex); frames = p.content; }
    std::atomic<bool> running{true};
    std::thread producer([&] {
      uint32_t sequence = 1;
      while (running.load()) {
        WorkerCommand command; command.kind = WorkerCommandKind::kClearOverlays;
        command.sequence = sequence++;
        worker.Submit(std::move(command));
      }
    });
    wait_for([&] { return p.content > frames + 2; });
    assert(worker.RequestFirmwareUpdate(321));
    poll([&] { return worker.State().update_completed_sequence == 321; });
    assert(worker.State().update_succeeded);
    running.store(false); producer.join();
    worker.RequestStop(); poll([&] { return worker.IsStopped(); });
  }
  join_tasks();
}

void test_stop_blocked() {
  Probe p; probe = &p; Renderer renderer; p.buffer = renderer.pixels;
  {
    Pixoo64Panel panel; RenderWorker worker(panel, renderer, 0); start_running(worker);
    block_next(worker, 1); worker.RequestStop();
    assert(!worker.IsStopped());
    { std::lock_guard<std::mutex> lock(p.mutex); assert(!p.released && p.shutdown == 0); }
    unblock(); poll([&] { return worker.IsStopped(); });
    { std::lock_guard<std::mutex> lock(p.mutex); assert(p.released && p.shutdown == 1); }
  }
  join_tasks();
}

void test_update_barrier() {
  Probe p; probe = &p; Renderer renderer; p.buffer = renderer.pixels;
  {
    Pixoo64Panel panel; RenderWorker worker(panel, renderer, 0); start_running(worker);
    assert(worker.RequestFirmwareUpdate(10));
    poll([&] { return worker.State().update_completed_sequence == 10; });
    assert(worker.State().update_succeeded);
    int frames, loops;
    { std::lock_guard<std::mutex> lock(p.mutex); assert(p.forced == 1); frames = p.content; loops = p.loops; }
    // Observe actual service iterations, rather than assuming a sleep paused rendering.
    wait_for([&] { return p.loops >= loops + 5; });
    { std::lock_guard<std::mutex> lock(p.mutex); assert(p.content == frames); }
    WorkerCommand reboot; reboot.kind = WorkerCommandKind::kReboot; reboot.sequence = 11; submit(worker, std::move(reboot));
    wait_for([&] { return p.loops >= loops + 7; });
    assert(worker.State().processed_sequence != 11);
    worker.ResumeFirmwareUpdate();
    processed(worker, 11);
    WorkerCommand empty_note; empty_note.kind = WorkerCommandKind::kNotification; empty_note.sequence = 12;
    submit(worker, std::move(empty_note));
    processed(worker, 12);
    std::vector<WorkerOutput> outputs; drain(worker, outputs);
    bool saw_reboot = false;
    for (const auto &out : outputs) saw_reboot |= out.kind == WorkerOutputKind::kReboot && out.sequence == 11;
    assert(saw_reboot);
    wait_for([&] { return p.content > frames; });
    worker.RequestStop(); poll([&] { return worker.IsStopped(); });
  }
  join_tasks();
}

void test_reboot_control_during_update() {
  Probe p; probe = &p; Renderer renderer; p.buffer = renderer.pixels;
  {
    Pixoo64Panel panel; RenderWorker worker(panel, renderer, 0);
    assert(!worker.RequestReboot());
    start_running(worker);
    assert(worker.RequestFirmwareUpdate(456));
    poll([&] { return worker.State().update_completed_sequence == 456; });
    for (uint32_t i = 0; i < 32; ++i) {
      WorkerCommand command; command.kind = WorkerCommandKind::kTimingPause; command.sequence = i + 1;
      submit(worker, std::move(command));
    }
    WorkerCommand overflow; overflow.kind = WorkerCommandKind::kTimingPause;
    assert(!worker.Submit(std::move(overflow)));
    assert(worker.RequestReboot());
    bool saw_reboot = false;
    poll([&] {
      WorkerOutput output;
      while (worker.ReadOutput(&output)) saw_reboot |= output.kind == WorkerOutputKind::kReboot;
      return saw_reboot;
    });
    assert(worker.State().processed_sequence == 0);
    worker.RequestStop();
    assert(!worker.RequestReboot());
    poll([&] { return worker.IsStopped(); });
    assert(!worker.RequestReboot());
  }
  join_tasks();
}

void test_start_failure() {
  {
    Probe p; probe = &p; Renderer renderer; Pixoo64Panel panel;
    fail_spiram_allocation = true;
    {
      RenderWorker worker(panel, renderer, 0);
      fail_spiram_allocation = false;
      assert(!worker.Start({}, "default", {}));
      assert(worker.IsStopped());
      WorkerCommand command;
      assert(!worker.Submit(std::move(command)));
      WorkerOutput output;
      assert(!worker.ReadOutput(&output));
    }
    assert(p.setup == 0 && p.shutdown == 0 && tasks.empty());
  }
  {
    Probe p; probe = &p; Renderer renderer; Pixoo64Panel panel;
    fail_task_creation = true;
    { RenderWorker worker(panel, renderer, 0); assert(!worker.Start({}, "default", {})); assert(worker.IsStopped()); }
    fail_task_creation = false;
    assert(p.setup == 0 && p.shutdown == 0);
  }
  for (bool panel_failure : {true, false}) {
    Probe p; probe = &p; p.setup_ok = !panel_failure; p.resolve_ok = panel_failure;
    Renderer renderer; p.buffer = renderer.pixels;
    { Pixoo64Panel panel; RenderWorker worker(panel, renderer, 0); assert(!worker.Start({}, "default", {})); }
    join_tasks(); assert(p.setup == 1 && p.shutdown == 1); assert(p.released == !panel_failure);
  }
}
} // namespace

TaskHandle_t xTaskGetCurrentTaskHandle() { return current_task ? static_cast<void *>(current_task) : reinterpret_cast<void *>(1); }
int xPortGetCoreID() { return current_task ? 1 : 0; }
int xTaskCreatePinnedToCore(void (*entry)(void *), const char *, uint32_t stack, void *arg, int priority, TaskHandle_t *out, int core) {
  assert(core == 1 && stack == 8 * 1024 && priority == 1);
  if (fail_task_creation) return 0;
  auto *task = new Task; tasks.push_back(task); if (out) *out = task;
  task->thread = std::thread([=] { current_task = task; entry(arg); current_task = nullptr; }); return pdPASS;
}
void xTaskNotifyGive(TaskHandle_t handle) {
  auto *task = static_cast<Task *>(handle); std::lock_guard<std::mutex> lock(task->mutex); ++task->notifications; task->cv.notify_one();
}
uint32_t ulTaskNotifyTake(int clear, TickType_t ticks) {
  assert(current_task); std::unique_lock<std::mutex> lock(current_task->mutex);
  current_task->cv.wait_for(lock, std::chrono::milliseconds(ticks), [] { return current_task->notifications != 0; });
  auto count = current_task->notifications; if (count) { if (clear) current_task->notifications = 0; else --current_task->notifications; } return count;
}
void vTaskDelay(TickType_t ticks) { std::this_thread::sleep_for(std::chrono::milliseconds(ticks)); }
void vTaskDelete(TaskHandle_t handle) { assert(handle == nullptr && current_task); }
void esp_restart() { std::abort(); }
namespace esphome {
uint32_t millis() { return clock_offset.load() + std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - epoch).count(); }
uint32_t micros() { return millis() * 1000; }
}
namespace esphome::pixoo64 {
Pixoo64Panel::~Pixoo64Panel() = default;
void Pixoo64Panel::setup() { assert(!current_task); }
void Pixoo64Panel::loop() { assert(!current_task); }
void Pixoo64Panel::on_shutdown() { assert(!current_task); }
bool Pixoo64Panel::teardown() { assert(!current_task); return true; }
void Pixoo64Panel::on_powerdown() { assert(!current_task); }
void Pixoo64Panel::dump_config() { assert(!current_task); }
bool Pixoo64Panel::WorkerSetup() { worker_only(); probe->owner = xTaskGetCurrentTaskHandle(); owner_task_.store(probe->owner); std::lock_guard<std::mutex> lock(probe->mutex); ++probe->setup; signal(); return probe->setup_ok; }
void Pixoo64Panel::WorkerLoop() { worker_only(); std::lock_guard<std::mutex> lock(probe->mutex); ++probe->loops; signal(); }
void Pixoo64Panel::WorkerShutdown() { worker_only(); std::lock_guard<std::mutex> lock(probe->mutex); ++probe->shutdown; signal(); }
void Pixoo64Panel::SetPower(bool) { worker_only(); }
bool Pixoo64Panel::Initialize() { worker_only(); return true; }
void Pixoo64Panel::SetBrightness(float) { worker_only(); }
bool Pixoo64Panel::Present(pixoo::FrameView frame, bool force) {
  worker_only(); assert(frame.data == probe->buffer && frame.size == 64);
  volatile uint8_t consumed = frame.data[0]; (void) consumed;
  std::lock_guard<std::mutex> lock(probe->mutex); ++probe->present; if (force) ++probe->forced; signal(); return true;
}
}
int main() {
  test_commands_and_backpressure(); test_delayed_commands_and_coalescing(); test_system_output_latch();
  test_flood_does_not_starve_frames_or_update(); test_stop_blocked(); test_update_barrier();
  test_reboot_control_during_update(); test_start_failure();
  std::cout << "render worker threaded tests passed; sizeof(WorkerCommand)=" << sizeof(WorkerCommand) << "; sizeof(RenderWorker)=" << sizeof(RenderWorker) << "\n";
}
