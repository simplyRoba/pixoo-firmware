#include "microphone_adapter.h"

#include <cinttypes>
#include <vector>

#include "esphome/core/hal.h"
#include "esphome/core/log.h"

namespace esphome::pixoo64::adapters {
namespace {

const char *const TAG = "pixoo64.microphone";

}  // namespace

void MicrophoneAdapter::setup() {
  if (this->microphone_ == nullptr || this->levels_sink_ == nullptr) {
    ESP_LOGE(TAG, "microphone and levels sink are required");
    this->mark_failed();
    return;
  }

  if (!this->processor_.ready()) {
    ESP_LOGE(TAG, "equalizer history allocation failed");
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
    this->mark_failed();
    return;
  }

  this->microphone_->add_data_callback(
      [this](const std::vector<uint8_t> &data) {
        this->processor_.AddSampleBytes(data.data(), data.size());
      });
  this->configured_ = true;
}

void MicrophoneAdapter::update() {
  this->ServiceMicrophone_();
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
    const bool stop_pending = this->microphone_stop_pending_;
    this->microphone_requested_ = true;
    this->microphone_stop_pending_ = false;
    this->SetEnableSwitch_(true);
    if (this->microphone_ != nullptr) {
      // A pending stop has not returned its listener request yet. An issued
      // stop has, so reclaim that request and let the microphone restart.
      if (!stop_pending)
        this->microphone_->start();
      this->microphone_stop_issued_ = false;
    }
    return;
  }

  if (!this->microphone_requested_)
    return;
  this->microphone_requested_ = false;
  if (this->microphone_ == nullptr) {
    this->SetEnableSwitch_(false);
    return;
  }

  // I2SAudioMicrophone::stop() is a no-op until the asynchronous start has
  // reached running, so wait to issue it. GPIO21 stays on until the stop has
  // completed so the I2S task can unwind safely.
  if (!this->microphone_->is_stopped()) {
    this->microphone_->stop();
    this->microphone_stop_issued_ = true;
    this->microphone_stop_pending_ = false;
  } else {
    // start() records a listener request before the microphone loop leaves the
    // stopped state. Give that loop a chance to observe it before stop().
    this->microphone_stop_pending_ = true;
    this->microphone_stop_pending_started_ms_ = millis();
  }
}

void MicrophoneAdapter::ServiceMicrophone_() {
  if (this->microphone_ == nullptr || this->microphone_requested_)
    return;

  if (this->microphone_stop_pending_) {
    if (!this->microphone_->is_stopped()) {
      this->microphone_->stop();
      this->microphone_stop_pending_ = false;
      this->microphone_stop_issued_ = true;
    } else if (millis() - this->microphone_stop_pending_started_ms_ >=
               kStopFallbackMs) {
      // A failed microphone ignores start() and remains stopped. Do not leave
      // its external enable line powered indefinitely.
      this->SetEnableSwitch_(false);
      this->microphone_stop_pending_ = false;
    }
  }
  if (this->microphone_stop_issued_ && this->microphone_->is_stopped()) {
    this->SetEnableSwitch_(false);
    this->microphone_stop_issued_ = false;
  }
}

void MicrophoneAdapter::SetEnableSwitch_(bool enabled) {
  if (this->enable_switch_ != nullptr) {
    if (enabled)
      this->enable_switch_->turn_on();
    else
      this->enable_switch_->turn_off();
  }
  this->microphone_enable_on_ = enabled && this->enable_switch_ != nullptr;
}

}  // namespace esphome::pixoo64::adapters
