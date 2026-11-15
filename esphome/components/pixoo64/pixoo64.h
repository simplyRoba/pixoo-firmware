#pragma once

#include <driver/spi_master.h>
#include <esp_attr.h>

#include <atomic>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <cstddef>
#include <cstdint>

#include "esphome/components/output/float_output.h"
#include "esphome/core/component.h"
#include "firmware_app.h"
#include "pixoo_frame.h"
#include "pixoo_output.h"
#include "pixoo_uart.h"

namespace esphome::pixoo64 {

struct TransportCounters {
  uint32_t completed_frames{0};
  uint32_t busy_drops{0};
};

class Pixoo64Panel final : public Component, public pixoo::PanelPort {
 public:
  ~Pixoo64Panel() override;

  void setup() override;
  void loop() override;
  bool WorkerSetup();
  void WorkerLoop();
  void WorkerShutdown();
  void on_shutdown() override;
  bool teardown() override;
  void on_powerdown() override;
  void dump_config() override;
  float get_setup_priority() const override {
    return setup_priority::HARDWARE - 0.5f;
  }

  void set_power_output(output::FloatOutput *power_output) {
    if (this->CanConfigure_()) this->power_output_ = power_output;
  }
  void set_spi_mosi_pin(int pin) { if (this->CanConfigure_()) this->spi_mosi_pin_ = pin; }
  void set_spi_sclk_pin(int pin) { if (this->CanConfigure_()) this->spi_sclk_pin_ = pin; }
  void set_spi_cs_pin(int pin) { if (this->CanConfigure_()) this->spi_cs_pin_ = pin; }
  void set_uart_rx_pin(int pin) { if (this->CanConfigure_()) this->uart_rx_pin_ = pin; }
  void set_spi_clock_hz(uint32_t clock_hz) { if (this->CanConfigure_()) this->spi_clock_hz_ = clock_hz; }
  void set_uart_baud(uint32_t baud) { if (this->CanConfigure_()) this->uart_baud_ = baud; }
  void set_soft_start_duration_ms(uint32_t duration_ms) {
    if (this->CanConfigure_()) this->soft_start_duration_ms_ = duration_ms;
  }
  void set_soft_start_period_ms(uint32_t period_ms) {
    if (this->CanConfigure_()) this->soft_start_period_ms_ = period_ms;
  }

  void SetPower(bool on) override;
  uint32_t LastPowerOnDelayMs() const override {
    return this->OnOwnerTask_() ? this->last_power_on_delay_ms_ : 0;
  }
  bool Initialize() override;
  void SetBrightness(float brightness) override;
  bool Present(pixoo::FrameView frame, bool force) override;
  TransportCounters GetTransportCounters() const {
    return this->OnOwnerTask_() ? this->transport_counters_ : TransportCounters{};
  }

 protected:
  static constexpr uint32_t kMaxFrameIntervalMs = 1000;

  enum class LifecycleState : uint8_t {
    kNew,
    kSettingUp,
    kReady,
    kStopping,
    kReleased,
    kFailed,
  };

  bool CanConfigure_() const {
    return this->owner_task_.load(std::memory_order_acquire) == nullptr;
  }
  bool OnOwnerTask_() const {
    const auto owner = this->owner_task_.load(std::memory_order_acquire);
    return owner != nullptr && owner == xTaskGetCurrentTaskHandle();
  }
  static void IRAM_ATTR spi_post_callback_(spi_transaction_t *transaction);
  bool queue_frame_();
  bool reap_frame_(bool wait);
  [[noreturn]] void transport_fatal_();
  std::atomic<TaskHandle_t> owner_task_{nullptr};
  std::atomic<bool> resources_released_{true};
  std::atomic<bool> worker_failed_{false};
  spi_transaction_t *transactions_{nullptr};
  size_t queued_{0};
  size_t reaped_{0};
  esp_err_t completion_gpio_error_{ESP_OK};
  TransportCounters transport_counters_{};
  bool IsOperational_() const;
  void FailSetup_(const char *operation, esp_err_t error);
  void StopOperations_();
  void ReleaseResources_();
  bool send_bytes_(const uint8_t *bytes, size_t size);
  bool send_transaction_(const uint8_t *frame, size_t frame_size,
                         const uint8_t *continuation, size_t continuation_size);
  bool send_control_transaction_(pixoo::Cmd cmd, const uint8_t *payload,
                                 size_t payload_size);
  bool allocate_transfer_buffers_();
  void release_transfer_buffers_();
  void poll_panel_uart_();
  bool set_power_rail_(bool on);
  bool set_cs_inactive_();
  bool soft_start_rail_();

  spi_device_handle_t panel_spi_{nullptr};
  pixoo::PanelUartParser panel_uart_;
  pixoo::FrameOutput output_;
  uint8_t *full_transaction_{nullptr};
  size_t full_transaction_size_{0};
  uint8_t *continuation_{nullptr};
  size_t continuation_size_{0};
  output::FloatOutput *power_output_{nullptr};
  int spi_mosi_pin_{0};
  int spi_sclk_pin_{0};
  int spi_cs_pin_{0};
  int uart_rx_pin_{0};
  uint32_t spi_clock_hz_{0};
  uint32_t uart_baud_{0};
  uint32_t soft_start_duration_ms_{0};
  uint32_t soft_start_period_ms_{0};
  float brightness_{1.0f};
  bool transfer_storage_acquired_{false};
  bool spi_bus_initialized_{false};
  bool spi_device_added_{false};
  bool uart_driver_installed_{false};
  bool cs_gpio_configured_{false};
  LifecycleState lifecycle_state_{LifecycleState::kNew};
  bool power_on_{false};
  uint32_t last_power_on_delay_ms_{0};
  bool panel_version_seen_{false};
};

}  // namespace esphome::pixoo64
