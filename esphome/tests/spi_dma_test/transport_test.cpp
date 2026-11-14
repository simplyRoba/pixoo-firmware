#include "pixoo64.h"
#include <algorithm>
#include <cassert>
#include <cstdlib>
#include <deque>
#include <stdexcept>
#include <vector>

using namespace esphome::pixoo64;
namespace fake {
struct Entry { spi_transaction_t *p; bool done; };
std::deque<Entry> queue;
std::vector<spi_transaction_t> descriptors;
std::vector<uint8_t> bytes;
void (*callback)(spi_transaction_t *) = nullptr;
int cs = 1, fail = -1, attempts = 0, frees = 0;
bool gpio_fail = false, wrong_result = false;
TaskHandle_t task = reinterpret_cast<void *>(1);
void complete() {
  for (auto &e : queue) if (!e.done) { e.done = true; callback(e.p); return; }
}
void reset() {
  assert(queue.empty()); descriptors.clear(); bytes.clear();
  cs = 1; fail = -1; attempts = frees = 0; gpio_fail = wrong_result = false;
  task = reinterpret_cast<void *>(1);
}
}
TaskHandle_t xTaskGetCurrentTaskHandle() { return fake::task; }
void esp_restart() { throw std::runtime_error("restart"); }
void heap_caps_free(void *p) { assert(fake::queue.empty()); if (p) ++fake::frees; free(p); }
esp_err_t gpio_set_level(int, int level) {
  if (level == 1 && fake::gpio_fail) { fake::gpio_fail = false; return ESP_FAIL; }
  fake::cs = level; return ESP_OK;
}
esp_err_t spi_bus_initialize(int, const spi_bus_config_t *, int) { return ESP_OK; }
esp_err_t spi_bus_add_device(int, const spi_device_interface_config_t *cfg, spi_device_handle_t *out) {
  assert(cfg->queue_size == 5 && cfg->post_cb); fake::callback = cfg->post_cb;
  *out = reinterpret_cast<void *>(1); return ESP_OK;
}
esp_err_t spi_bus_remove_device(spi_device_handle_t) { assert(fake::queue.empty()); return ESP_OK; }
esp_err_t spi_bus_free(int) { return ESP_OK; }
esp_err_t spi_device_queue_trans(spi_device_handle_t, spi_transaction_t *p, unsigned timeout) {
  assert(!timeout && fake::cs == 0);
  if (fake::attempts++ == fake::fail) return ESP_FAIL;
  fake::descriptors.push_back(*p);
  auto *b = static_cast<const uint8_t *>(p->tx_buffer);
  fake::bytes.insert(fake::bytes.end(), b, b + p->length / 8);
  fake::queue.push_back({p, false}); return ESP_OK;
}
esp_err_t spi_device_get_trans_result(spi_device_handle_t, spi_transaction_t **out, unsigned timeout) {
  assert(!fake::queue.empty());
  if (!fake::queue.front().done) { if (!timeout) return ESP_ERR_TIMEOUT; fake::complete(); }
  if (fake::wrong_result) { *out = nullptr; return ESP_OK; }
  *out = fake::queue.front().p; fake::queue.pop_front(); return ESP_OK;
}
esp_err_t spi_device_transmit(spi_device_handle_t, spi_transaction_t *p) {
  assert(fake::queue.empty() && fake::cs == 0);
  fake::callback(p); return ESP_OK;
}
struct Power : esphome::output::FloatOutput {
  float level = 0;
  void set_level(float v) override { if (v == 0) assert(fake::queue.empty()); level = v; }
};
struct Fixture {
  Power power; Pixoo64Panel panel; std::vector<uint8_t> frame = std::vector<uint8_t>(12288, 42);
  Fixture() {
    fake::reset(); panel.set_power_output(&power); panel.set_soft_start_duration_ms(1);
    panel.set_soft_start_period_ms(1); panel.setup(); fake::task = reinterpret_cast<void *>(2);
    assert(panel.WorkerSetup()); panel.SetPower(true); assert(panel.Initialize()); fake::bytes.clear();
  }
  ~Fixture() { fake::task = reinterpret_cast<void *>(2); panel.WorkerShutdown(); fake::task = reinterpret_cast<void *>(1); }
  pixoo::FrameView view() { return {frame.data(), frame.size()}; }
};
void layout_busy_and_forced() {
  Fixture f;
  for (size_t i=0; i<f.frame.size(); ++i) f.frame[i] = static_cast<uint8_t>(i*37+11);
  assert(f.panel.Present(f.view(), false));
  std::vector<uint8_t> expected(pixoo::kFullFrameWireBytes + pixoo::kFullFrameContinuationBytes);
  auto n = pixoo::EncodeFullFrameTo(f.frame.data(), f.frame.size(), expected.data(), expected.size());
  assert(pixoo::EncodeContinuationTo(n, true, expected.data()+n, expected.size()-n) == 240);
  assert(fake::bytes == expected && fake::cs == 0 && fake::queue.size() == 5);
  const size_t sizes[] = {4096,4096,4096,5,240};
  for (int i=0; i<5; ++i) {
    assert(fake::descriptors[i].length == sizes[i]*8);
    assert(fake::descriptors[i].flags == SPI_TRANS_DMA_BUFFER_ALIGN_MANUAL);
    assert((fake::descriptors[i].user != nullptr) == (i == 4));
  }
  auto *base = static_cast<const uint8_t *>(fake::descriptors[0].tx_buffer);
  std::vector<uint8_t> saved(base, base+12293);
  std::fill(f.frame.begin(), f.frame.end(), 99);
  assert(!f.panel.Present(f.view(), false)); assert(std::equal(saved.begin(), saved.end(), base));
  fake::task = reinterpret_cast<void *>(1); assert(!f.panel.Present(f.view(), true)); f.panel.SetPower(false);
  assert(fake::queue.size() == 5 && f.power.level == 1); fake::task = reinterpret_cast<void *>(2);
  for (int i=0; i<4; ++i) { fake::complete(); assert(fake::cs == 0); }
  fake::complete(); assert(fake::cs == 1); f.panel.WorkerLoop(); assert(fake::queue.empty());
  assert(f.panel.Present(f.view(), true)); assert(fake::queue.empty() && fake::cs == 1);
}
void partial_failures() {
  for (int i=0; i<5; ++i) {
    Fixture f; fake::fail = i; assert(!f.panel.Present(f.view(), false));
    assert(fake::queue.empty() && fake::cs == 1 && fake::attempts == i+1);
    fake::fail = -1; assert(f.panel.Present(f.view(), false));
    assert(f.panel.Initialize()); assert(fake::queue.empty());
  }
}
void shutdown_and_failures() {
  {
    Fixture f; assert(f.panel.Present(f.view(), false)); fake::task = reinterpret_cast<void *>(1);
    f.panel.on_shutdown(); assert(!f.panel.teardown() && fake::queue.size() == 5);
    fake::task = reinterpret_cast<void *>(2); f.panel.WorkerShutdown();
    assert(f.panel.teardown() && fake::queue.empty() && f.power.level == 0 && fake::frees == 3);
  }
  {
    Fixture f; assert(f.panel.Present(f.view(), false));
    for (int i=0; i<4; ++i) fake::complete(); fake::gpio_fail = true; fake::complete();
    f.panel.WorkerLoop(); assert(fake::queue.empty() && fake::cs == 1 && f.power.level == 0);
    fake::task = reinterpret_cast<void *>(1); f.panel.loop(); assert(f.panel.is_failed());
  }
  {
    Fixture f; assert(f.panel.Present(f.view(), false)); fake::complete(); fake::wrong_result = true;
    bool restarted = false; try { f.panel.WorkerLoop(); } catch (const std::runtime_error &) { restarted = true; }
    assert(restarted && fake::frees == 0); fake::wrong_result = false; f.panel.WorkerShutdown();
  }
  {
    Fixture f; fake::gpio_fail = true; assert(!f.panel.Initialize()); assert(f.power.level == 0);
    fake::task = reinterpret_cast<void *>(1); f.panel.loop(); assert(f.panel.is_failed());
  }
}
int main() { layout_busy_and_forced(); partial_failures(); shutdown_and_failures(); }
