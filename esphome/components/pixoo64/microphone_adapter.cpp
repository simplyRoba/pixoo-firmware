#include "microphone_adapter.h"

#include <cinttypes>
#include <new>

#include "esp_heap_caps.h"

#include "esphome/core/log.h"

namespace esphome::pixoo64::adapters {
namespace {

const char *const TAG = "pixoo64.microphone";

}  // namespace

void MicrophoneAdapter::CaptureDeleter::operator()(
    pixoo::EqualizerProcessor::CaptureStorage *storage) const {
  if (storage != nullptr) {
    storage->~CaptureStorage();
    heap_caps_free(storage);
  }
}

void MicrophoneAdapter::setup() {
  using CaptureStorage = pixoo::EqualizerProcessor::CaptureStorage;
  void *memory = heap_caps_malloc(sizeof(CaptureStorage), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (memory != nullptr)
    this->capture_.reset(new (memory) CaptureStorage);
  this->processor_.SetCaptureStorage(this->capture_.get());
  if (this->microphone_ == nullptr || this->levels_sink_ == nullptr) {
    ESP_LOGE(TAG, "microphone and levels sink are required");
    this->SetEnableSwitch_(false);
    this->mark_failed();
    return;
  }

  if (!this->processor_.ready()) {
    ESP_LOGE(TAG, "equalizer capture or history allocation failed");
    this->SetEnableSwitch_(false);
    this->mark_failed();
    return;
  }

  if (this->microphone_->is_failed()) {
    ESP_LOGE(TAG, "source microphone setup failed");
    this->SetEnableSwitch_(false);
    this->mark_failed();
    return;
  }

  const auto stream_info = this->microphone_->get_audio_stream_info();
  if (stream_info.get_bits_per_sample() != 32 ||
      stream_info.get_channels() != 1 ||
      stream_info.get_sample_rate() != 32000) {
    ESP_LOGE(TAG,
             "unsupported microphone stream: %u bits, %u channel(s), %" PRIu32
             " Hz; require 32-bit mono 32000 Hz",
             static_cast<unsigned int>(stream_info.get_bits_per_sample()),
             static_cast<unsigned int>(stream_info.get_channels()),
             stream_info.get_sample_rate());
    this->SetEnableSwitch_(false);
    this->mark_failed();
    return;
  }

  this->microphone_->add_data_callback(
      [this](const microphone::AudioCapture &capture) {
        this->processor_.AddSampleBytes(capture.data.data(), capture.data.size());
        this->capture_received_.store(true, std::memory_order_relaxed);
      });
  this->configured_ = true;
}

void MicrophoneAdapter::update() {
  this->ServiceMicrophone_();
  if (this->microphone_requested_ && !this->capture_logged_ &&
      this->capture_received_.load(std::memory_order_relaxed)) {
    ESP_LOGD(TAG, "capture running");
    this->capture_logged_ = true;
  }
  if (this->levels_sink_ == nullptr)
    return;

  float levels[pixoo::kBands];
  if (this->processor_.Poll(levels))
    this->levels_sink_->SetLevels(levels);
}

void MicrophoneAdapter::on_shutdown() {
  this->SetEnabled(false);
}

bool MicrophoneAdapter::teardown() {
  if (this->microphone_ == nullptr || this->microphone_->is_failed()) {
    this->SetEnableSwitch_(false);
    return true;
  }

  this->ServiceMicrophone_();
  this->microphone_->loop();
  this->ServiceMicrophone_();
  return this->microphone_->is_stopped() && !this->microphone_enable_on_;
}

void MicrophoneAdapter::on_powerdown() {
  this->SetEnableSwitch_(false);
}

void MicrophoneAdapter::dump_config() {
  ESP_LOGCONFIG(TAG, "Pixoo64 microphone adapter:");
  ESP_LOGCONFIG(TAG, "  Source microphone: %s",
                this->microphone_ == nullptr ? "not configured" : "configured");
  ESP_LOGCONFIG(TAG, "  Equalizer levels sink: %s",
                this->levels_sink_ == nullptr ? "not configured" : "configured");
}

void MicrophoneAdapter::SetEnabled(bool enabled) {
  if (!this->configured_) {
    if (!enabled)
      this->SetEnableSwitch_(false);
    return;
  }

  if (enabled) {
    if (this->microphone_requested_)
      return;
    this->microphone_requested_ = true;
    this->microphone_stop_issued_ = false;
    this->capture_received_.store(false, std::memory_order_relaxed);
    this->capture_logged_ = false;
    this->SetEnableSwitch_(true);
    if (this->microphone_ != nullptr)
      this->microphone_->start();
    return;
  }

  if (!this->microphone_requested_)
    return;
  this->microphone_requested_ = false;
  if (this->microphone_ == nullptr) {
    this->SetEnableSwitch_(false);
    return;
  }

  // stop() also returns a listener request that the microphone loop has not
  // acted on yet. GPIO21 stays on until the microphone reports stopped so the
  // I2S task can unwind safely.
  this->microphone_->stop();
  this->microphone_stop_issued_ = true;
}

void MicrophoneAdapter::ServiceMicrophone_() {
  if (this->microphone_ == nullptr || this->microphone_requested_)
    return;

  if (this->microphone_stop_issued_ && this->microphone_->is_stopped()) {
    this->SetEnableSwitch_(false);
    this->microphone_stop_issued_ = false;
  }
}

void MicrophoneAdapter::SetEnableSwitch_(bool enabled) {
  if (enabled != this->microphone_enable_on_ && this->enable_switch_ != nullptr)
    ESP_LOGD(TAG, "enable line %s", enabled ? "on" : "off");
  if (this->enable_switch_ != nullptr) {
    if (enabled)
      this->enable_switch_->turn_on();
    else
      this->enable_switch_->turn_off();
  }
  this->microphone_enable_on_ = enabled && this->enable_switch_ != nullptr;
}

}  // namespace esphome::pixoo64::adapters
