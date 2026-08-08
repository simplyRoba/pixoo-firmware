#include "pixoo64.h"

#include <driver/gpio.h>
#include <driver/uart.h>
#include <esp_heap_caps.h>
#include <esp_system.h>

#include <cmath>
#include <cstdint>
#include <utility>

#include "esphome/core/hal.h"
#include "esphome/core/log.h"

namespace esphome::pixoo64 {
namespace {

static const char *const TAG = "pixoo64.panel";
constexpr uart_port_t kPanelUartPort = UART_NUM_2;
constexpr uint32_t kPanelDmaCaps =
    MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;

bool IsDmaAligned(const void *pointer) {
  return (reinterpret_cast<uintptr_t>(pointer) % alignof(uint32_t)) == 0;
}

const char *ResetReasonName(esp_reset_reason_t reason) {
  switch (reason) {
    case ESP_RST_POWERON:   return "POWERON";
    case ESP_RST_EXT:       return "EXT";
    case ESP_RST_SW:        return "SW";
    case ESP_RST_PANIC:     return "PANIC";
    case ESP_RST_INT_WDT:   return "INT_WDT";
    case ESP_RST_TASK_WDT:  return "TASK_WDT";
    case ESP_RST_WDT:       return "WDT";
    case ESP_RST_DEEPSLEEP: return "DEEPSLEEP";
    case ESP_RST_BROWNOUT:  return "BROWNOUT";
    case ESP_RST_SDIO:      return "SDIO";
    default:                return "UNKNOWN";
  }
}

void LogIdfFailure(const char *operation, esp_err_t error) {
  ESP_LOGE(TAG, "%s failed: %s", operation, esp_err_to_name(error));
}

}  // namespace

Pixoo64Panel::~Pixoo64Panel() {
  this->StopOperations_();
  this->ReleaseResources_();
}

bool Pixoo64Panel::IsOperational_() const {
  return this->lifecycle_state_ == LifecycleState::kReady && !this->is_failed();
}

void Pixoo64Panel::FailSetup_(const char *operation, esp_err_t error) {
  LogIdfFailure(operation, error);
  this->StopOperations_();
  this->ReleaseResources_();
  this->lifecycle_state_ = LifecycleState::kFailed;
  this->mark_failed();
}

void Pixoo64Panel::StopOperations_() {
  if (this->lifecycle_state_ != LifecycleState::kFailed &&
      this->lifecycle_state_ != LifecycleState::kReleased) {
    this->lifecycle_state_ = LifecycleState::kStopping;
  }
  this->power_on_ = false;
  this->last_power_on_delay_ms_ = 0;
  this->set_power_rail_(false);
  this->set_cs_inactive_();
}

void Pixoo64Panel::ReleaseResources_() {
  this->StopOperations_();

  bool spi_device_released = !this->spi_device_added_;
  if (this->spi_device_added_) {
    const esp_err_t error = spi_bus_remove_device(this->panel_spi_);
    if (error != ESP_OK) {
      LogIdfFailure("remove panel SPI device", error);
    } else {
      this->panel_spi_ = nullptr;
      this->spi_device_added_ = false;
      spi_device_released = true;
    }
  }
  if (this->spi_bus_initialized_ && spi_device_released) {
    const esp_err_t error = spi_bus_free(SPI2_HOST);
    if (error != ESP_OK) {
      LogIdfFailure("free panel SPI bus", error);
    } else {
      this->spi_bus_initialized_ = false;
    }
  }
  if (this->uart_driver_installed_) {
    const esp_err_t error = uart_driver_delete(kPanelUartPort);
    if (error != ESP_OK) {
      LogIdfFailure("delete panel UART driver", error);
    } else {
      this->uart_driver_installed_ = false;
    }
  }
  this->release_transfer_buffers_();

  // CS has no driver allocation to release. Keep it configured output-high so
  // the panel remains deselected until the ESP32 resets.
  if (this->lifecycle_state_ != LifecycleState::kFailed &&
      !this->spi_device_added_ && !this->spi_bus_initialized_ &&
      !this->uart_driver_installed_ && !this->transfer_storage_acquired_) {
    this->lifecycle_state_ = LifecycleState::kReleased;
  }
}

bool Pixoo64Panel::allocate_transfer_buffers_() {
  this->release_transfer_buffers_();
  this->full_transaction_size_ = pixoo::kFullFrameWireBytes;
  this->continuation_size_ = pixoo::kFullFrameContinuationBytes;
  this->full_transaction_ = static_cast<uint8_t *>(
      heap_caps_malloc(this->full_transaction_size_, kPanelDmaCaps));
  this->continuation_ = static_cast<uint8_t *>(
      heap_caps_malloc(this->continuation_size_, kPanelDmaCaps));
  if (this->full_transaction_ == nullptr || this->continuation_ == nullptr ||
      !IsDmaAligned(this->full_transaction_) ||
      !IsDmaAligned(this->continuation_) ||
      !this->output_.ConfigureCapacity(pixoo::kFramePayloadBytes)) {
    this->release_transfer_buffers_();
    return false;
  }
  this->transfer_storage_acquired_ = true;
  return true;
}

void Pixoo64Panel::release_transfer_buffers_() {
  if (this->full_transaction_ != nullptr)
    heap_caps_free(this->full_transaction_);
  if (this->continuation_ != nullptr)
    heap_caps_free(this->continuation_);
  this->full_transaction_ = nullptr;
  this->full_transaction_size_ = 0;
  this->continuation_ = nullptr;
  this->continuation_size_ = 0;
  if (this->transfer_storage_acquired_)
    this->output_.ConfigureCapacity(0);
  this->transfer_storage_acquired_ = false;
}

void Pixoo64Panel::setup() {
  if (this->lifecycle_state_ != LifecycleState::kNew) {
    ESP_LOGW(TAG, "panel setup requested outside construction state");
    if (this->lifecycle_state_ == LifecycleState::kFailed)
      this->mark_failed();
    return;
  }
  this->lifecycle_state_ = LifecycleState::kSettingUp;

  if (this->power_output_ == nullptr) {
    this->FailSetup_("resolve panel power output", ESP_ERR_INVALID_ARG);
    return;
  }
  this->set_power_rail_(false);

  const esp_reset_reason_t reset_reason = esp_reset_reason();
  ESP_LOGI(TAG, "reset reason: %s (%d)", ResetReasonName(reset_reason),
           static_cast<int>(reset_reason));

  esp_err_t error = gpio_set_level(static_cast<gpio_num_t>(this->spi_cs_pin_), 1);
  if (error != ESP_OK) {
    this->FailSetup_("latch panel SPI CS inactive", error);
    return;
  }
  error = gpio_set_direction(static_cast<gpio_num_t>(this->spi_cs_pin_),
                             GPIO_MODE_OUTPUT);
  if (error != ESP_OK) {
    this->FailSetup_("configure panel SPI CS GPIO", error);
    return;
  }
  this->cs_gpio_configured_ = true;
  if (!this->set_cs_inactive_()) {
    this->FailSetup_("drive panel SPI CS inactive", ESP_FAIL);
    return;
  }

  if (!this->allocate_transfer_buffers_()) {
    this->FailSetup_("allocate retained panel transfer storage",
                     ESP_ERR_NO_MEM);
    return;
  }

  spi_bus_config_t bus_config = {};
  bus_config.mosi_io_num = this->spi_mosi_pin_;
  bus_config.miso_io_num = -1;
  bus_config.sclk_io_num = this->spi_sclk_pin_;
  bus_config.quadwp_io_num = -1;
  bus_config.quadhd_io_num = -1;
  bus_config.max_transfer_sz = 12544;
  error = spi_bus_initialize(SPI2_HOST, &bus_config, SPI_DMA_CH_AUTO);
  if (error != ESP_OK) {
    this->FailSetup_("initialize panel SPI bus", error);
    return;
  }
  this->spi_bus_initialized_ = true;

  spi_device_interface_config_t device_config = {};
  device_config.clock_speed_hz = static_cast<int>(this->spi_clock_hz_);
  device_config.mode = 0;
  device_config.spics_io_num = -1;
  device_config.queue_size = 1;
  error = spi_bus_add_device(SPI2_HOST, &device_config, &this->panel_spi_);
  if (error != ESP_OK) {
    this->FailSetup_("add panel SPI device", error);
    return;
  }
  this->spi_device_added_ = true;

  uart_config_t uart_config = {};
  uart_config.baud_rate = static_cast<int>(this->uart_baud_);
  uart_config.data_bits = UART_DATA_8_BITS;
  uart_config.parity = UART_PARITY_DISABLE;
  uart_config.stop_bits = UART_STOP_BITS_1;
  uart_config.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
  uart_config.source_clk = UART_SCLK_APB;
  error = uart_driver_install(kPanelUartPort, 256, 0, 0, nullptr, 0);
  if (error != ESP_OK) {
    this->FailSetup_("install panel UART driver", error);
    return;
  }
  this->uart_driver_installed_ = true;

  error = uart_param_config(kPanelUartPort, &uart_config);
  if (error != ESP_OK) {
    this->FailSetup_("configure panel UART", error);
    return;
  }
  error = uart_set_pin(kPanelUartPort, UART_PIN_NO_CHANGE, this->uart_rx_pin_,
                       UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
  if (error != ESP_OK) {
    this->FailSetup_("set panel UART RX pin", error);
    return;
  }

  this->power_on_ = false;
  this->last_power_on_delay_ms_ = 0;
  this->panel_version_seen_ = false;
  this->lifecycle_state_ = LifecycleState::kReady;
  ESP_LOGI(TAG, "panel transport ready");
}

void Pixoo64Panel::loop() {
  if (!this->IsOperational_() || !this->uart_driver_installed_)
    return;
  this->poll_panel_uart_();
}

void Pixoo64Panel::on_shutdown() { this->StopOperations_(); }

bool Pixoo64Panel::teardown() {
  this->ReleaseResources_();
  return !this->spi_device_added_ && !this->spi_bus_initialized_ &&
         !this->uart_driver_installed_ && !this->transfer_storage_acquired_;
}

void Pixoo64Panel::on_powerdown() { this->StopOperations_(); }

void Pixoo64Panel::poll_panel_uart_() {
  uint8_t byte;
  while (uart_read_bytes(kPanelUartPort, &byte, 1, 0) == 1) {
    pixoo::PanelUartFrame frame;
    if (!this->panel_uart_.Feed(byte, &frame))
      continue;
    if (frame.cmd == static_cast<uint8_t>(pixoo::Cmd::kInit) &&
        frame.payload_size == 4) {
      ESP_LOGI(TAG, "panel ledboard version: %02x %02x %02x %02x",
               frame.payload[0], frame.payload[1], frame.payload[2],
               frame.payload[3]);
      this->panel_version_seen_ = true;
    } else {
      ESP_LOGD(TAG, "panel UART frame: cmd=0x%02x len=%u", frame.cmd,
               static_cast<unsigned>(frame.payload_size));
    }
  }
}

void Pixoo64Panel::SetPower(bool on) {
  if (!this->IsOperational_())
    return;

  if (on == this->power_on_) {
    this->last_power_on_delay_ms_ = 0;
    return;
  }

  if (on) {
    if (!this->soft_start_rail_())
      return;
    this->last_power_on_delay_ms_ = this->soft_start_duration_ms_;
  } else {
    if (!this->set_power_rail_(false))
      return;
    this->last_power_on_delay_ms_ = 0;
  }
  this->power_on_ = on;
  ESP_LOGI(TAG, "panel power %s", on ? "on" : "off");
}

bool Pixoo64Panel::Initialize() {
  if (!this->IsOperational_() || this->panel_spi_ == nullptr ||
      !this->power_on_ || this->full_transaction_ == nullptr ||
      this->continuation_ == nullptr) {
    return false;
  }
  const uint8_t init_payload[] = {0x00};
  const uint8_t white_balance_payload[] = {56, 56, 56};
  if (!this->send_control_transaction_(pixoo::Cmd::kInit, init_payload,
                                       sizeof(init_payload)))
    return false;
  if (!this->send_control_transaction_(pixoo::Cmd::kWhiteBalance,
                                       white_balance_payload,
                                       sizeof(white_balance_payload)))
    return false;
  this->output_.Reset();
  ESP_LOGI(TAG, "panel initialized");
  return true;
}

void Pixoo64Panel::SetBrightness(float brightness) {
  if (!this->IsOperational_())
    return;

  if (!std::isfinite(brightness) || brightness < 0.0f)
    brightness = 0.0f;
  if (brightness > 1.0f)
    brightness = 1.0f;
  this->brightness_ = brightness;
}

bool Pixoo64Panel::Present(pixoo::FrameView frame, bool force) {
  if (!this->IsOperational_() || this->panel_spi_ == nullptr ||
      !this->power_on_ || this->full_transaction_ == nullptr ||
      this->continuation_ == nullptr || frame.data == nullptr ||
      frame.size != static_cast<size_t>(pixoo::kFramePayloadBytes))
    return false;

  uint8_t *const payload = this->full_transaction_ + 4;
  if (!this->output_.Prepare(frame.data, frame.size, this->brightness_,
                             millis(), kMaxFrameIntervalMs, payload,
                             this->full_transaction_size_ - 4, force))
    return true;

  const size_t transaction_size =
      pixoo::EncodeFullFrameTo(payload, frame.size, this->full_transaction_,
                               this->full_transaction_size_);
  const size_t continuation_size = pixoo::EncodeContinuationTo(
      transaction_size, true, this->continuation_, this->continuation_size_);
  const bool presented =
      transaction_size != 0 && continuation_size != 0 &&
      this->send_transaction_(this->full_transaction_, transaction_size,
                              this->continuation_, continuation_size);
  if (!presented)
    this->output_.Reset();
  return presented;
}

bool Pixoo64Panel::set_power_rail_(bool on) {
  if (this->power_output_ == nullptr)
    return false;
  this->power_output_->set_level(on ? 1.0f : 0.0f);
  return true;
}

bool Pixoo64Panel::set_cs_inactive_() {
  if (!this->cs_gpio_configured_)
    return false;
  const esp_err_t error =
      gpio_set_level(static_cast<gpio_num_t>(this->spi_cs_pin_), 1);
  if (error != ESP_OK) {
    LogIdfFailure("drive panel SPI CS inactive", error);
    return false;
  }
  return true;
}

bool Pixoo64Panel::soft_start_rail_() {
  if (this->power_output_ == nullptr || this->soft_start_duration_ms_ == 0 ||
      this->soft_start_period_ms_ == 0) {
    this->set_power_rail_(false);
    return false;
  }

  this->power_output_->set_level(0.0f);
  uint32_t elapsed_ms = 0;
  while (elapsed_ms < this->soft_start_duration_ms_) {
    const uint32_t remaining_ms = this->soft_start_duration_ms_ - elapsed_ms;
    const uint32_t step_ms = this->soft_start_period_ms_ < remaining_ms
                                 ? this->soft_start_period_ms_
                                 : remaining_ms;
    delay(step_ms);
    elapsed_ms += step_ms;
    this->power_output_->set_level(
        static_cast<float>(elapsed_ms) /
        static_cast<float>(this->soft_start_duration_ms_));
  }

  // Full duty is a static high output, not continuous switching. ESPHome's
  // LEDC adapter stops the channel and latches the pin high at level 1.0.
  return this->set_power_rail_(true);
}

void Pixoo64Panel::dump_config() {
  ESP_LOGCONFIG(TAG, "Pixoo64 panel adapter:");
  ESP_LOGCONFIG(TAG, "  SPI: MOSI GPIO%d, SCLK GPIO%d, CS GPIO%d at %lu Hz",
                this->spi_mosi_pin_, this->spi_sclk_pin_, this->spi_cs_pin_,
                static_cast<unsigned long>(this->spi_clock_hz_));
  ESP_LOGCONFIG(TAG, "  Panel UART: RX GPIO%d at %lu baud (receive-only)",
                this->uart_rx_pin_,
                static_cast<unsigned long>(this->uart_baud_));
  ESP_LOGCONFIG(TAG, "  Panel soft-start: %lu ms over %lu ms periods",
                static_cast<unsigned long>(this->soft_start_duration_ms_),
                static_cast<unsigned long>(this->soft_start_period_ms_));
  ESP_LOGCONFIG(TAG, "  Panel power rail: hardware PWM output");
}

bool Pixoo64Panel::send_bytes_(const uint8_t *bytes, size_t size) {
  if (!this->IsOperational_() || this->panel_spi_ == nullptr)
    return false;
  while (size != 0) {
    const size_t chunk = size > 4096 ? 4096 : size;
    spi_transaction_t transaction = {};
    transaction.length = chunk * 8;
    transaction.tx_buffer = bytes;
    const esp_err_t error =
        spi_device_transmit(this->panel_spi_, &transaction);
    if (error != ESP_OK) {
      LogIdfFailure("transmit panel SPI bytes", error);
      return false;
    }
    bytes += chunk;
    size -= chunk;
  }
  return true;
}

bool Pixoo64Panel::send_transaction_(const uint8_t *frame, size_t frame_size,
                                     const uint8_t *continuation,
                                     size_t continuation_size) {
  if (!this->IsOperational_() || frame == nullptr || frame_size == 0 ||
      (continuation_size != 0 && continuation == nullptr))
    return false;
  const esp_err_t select_error =
      gpio_set_level(static_cast<gpio_num_t>(this->spi_cs_pin_), 0);
  if (select_error != ESP_OK) {
    LogIdfFailure("assert panel SPI CS", select_error);
    this->set_cs_inactive_();
    return false;
  }
  const bool sent = this->send_bytes_(frame, frame_size) &&
                    (continuation_size == 0 ||
                     this->send_bytes_(continuation, continuation_size));
  const bool deselected = this->set_cs_inactive_();
  if (!sent)
    ESP_LOGW(TAG, "panel SPI transfer failed");
  return sent && deselected;
}

bool Pixoo64Panel::send_control_transaction_(pixoo::Cmd cmd,
                                             const uint8_t *payload,
                                             size_t payload_size) {
  if (!this->IsOperational_())
    return false;
  const size_t frame_size =
      pixoo::EncodeFrameTo(cmd, payload, payload_size, this->full_transaction_,
                           this->full_transaction_size_);
  if (frame_size == 0)
    return false;
  const size_t continuation_size = pixoo::EncodeContinuationTo(
      frame_size, false, this->continuation_, this->continuation_size_);
  if (continuation_size == 0 && frame_size < pixoo::kControlDmaTarget &&
      pixoo::kControlDmaTarget - frame_size >= pixoo::kMinFrameBytes)
    return false;
  return this->send_transaction_(this->full_transaction_, frame_size,
                                 this->continuation_, continuation_size);
}

}  // namespace esphome::pixoo64
