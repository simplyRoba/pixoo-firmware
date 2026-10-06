#include "ddp_dashboard.h"

#include <cerrno>
#include <fcntl.h>
#include <cinttypes>
#include <new>

#ifdef ESP_PLATFORM
#include "esp_heap_caps.h"
#endif

#include "esphome/core/hal.h"
#include "esphome/core/log.h"

namespace esphome::pixoo64::dashboard {
namespace {
constexpr const char *kTag = "pixoo64.ddp";
constexpr uint16_t kPort = 4048;
constexpr uint32_t kRetryMs = 1000;
constexpr uint32_t kReceiveBudgetUs = 1000;
constexpr size_t kReadAttempts = 32;
}  // namespace

void DdpDashboard::FrameStorageDeleter::operator()(
    pixoo::ddp::FrameBuffer *storage) const {
#ifdef ESP_PLATFORM
  storage->~FrameBuffer();
  heap_caps_free(storage);
#else
  delete storage;
#endif
}

bool DdpDashboard::EnsureFrameStorage_() {
  if (this->frames_ != nullptr)
    return true;
  if (this->allocation_attempted_)
    return false;
  this->allocation_attempted_ = true;
#ifdef ESP_PLATFORM
  void *memory = heap_caps_malloc(sizeof(pixoo::ddp::FrameBuffer),
                                 MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (memory == nullptr)
    return false;
  this->frames_.reset(new (memory) pixoo::ddp::FrameBuffer{});
#else
  this->frames_.reset(new (std::nothrow) pixoo::ddp::FrameBuffer{});
#endif
  return this->frames_ != nullptr;
}

void DdpDashboard::Render(display::Display &display) const {
  if (this->active_)
    this->metrics_.RecordRender(this->frames_ != nullptr && this->frames_->has_frame());
  if (this->frames_ == nullptr) {
    display.fill(Color(0, 0, 0));
    return;
  }
  const auto &frame = this->frames_->frame();
  for (size_t y = 0; y < pixoo::ddp::kFrameHeight; ++y) {
    for (size_t x = 0; x < pixoo::ddp::kFrameWidth; ++x) {
      const size_t offset = (y * pixoo::ddp::kFrameWidth + x) * 3;
      display.draw_pixel_at(
          x, y, Color(frame[offset], frame[offset + 1], frame[offset + 2]));
    }
  }
}

void DdpDashboard::OnShow(uint32_t now_ms) {
  if (this->active_)
    return;
  this->active_ = true;
  this->metrics_.Reset(now_ms);
  this->retry_pending_ = false;
  if (!this->EnsureFrameStorage_()) {
    this->status_set_warning();
    return;
  }
  this->frames_->Reset();
}

void DdpDashboard::OnHide(uint32_t now_ms) {
  this->Stop_(now_ms);
}

void DdpDashboard::CloseListener_() {
  if (this->listener_) {
    this->listener_->close();
    this->listener_.reset();
  }
}

void DdpDashboard::on_shutdown() {
  this->Stop_(millis());
}

void DdpDashboard::ReportMetrics_(uint32_t now_ms) {
  pixoo::ddp::MetricsSnapshot s;
  if (!this->active_ || !this->metrics_.Close(now_ms, &s))
    return;
  ESP_LOGI(kTag, "elapsed_ms=%" PRIu32 " received=%" PRIu32
           " rejected=%" PRIu32 " publications=%" PRIu32
           " rendered_revisions=%" PRIu32 " latest_revision=%" PRIu64
           " receive_passes=%" PRIu32 " receive_avg_us=%.1f receive_max_us=%" PRIu32
           " loop_gap_max_us=%" PRIu32 " socket_errors=%" PRIu32,
           s.elapsed_ms, s.received, s.rejected, s.publications,
           s.rendered_revisions, s.latest_revision, s.receive_passes,
           s.receive_average_us, s.receive_max_us, s.loop_gap_max_us, s.socket_errors);
#ifdef ESP_PLATFORM
  ESP_LOGI(kTag, "internal_free_bytes=%u psram_free_bytes=%u",
           static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
           static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)));
#endif
}

void DdpDashboard::Stop_(uint32_t now_ms) {
  this->ReportMetrics_(now_ms);
  this->active_ = false;
  this->CloseListener_();
  if (this->frames_ != nullptr)
    this->frames_->Reset();
  this->retry_pending_ = false;
  this->status_clear_warning();
}

void DdpDashboard::ListenerFailed_(uint32_t now_ms) {
  this->metrics_.RecordSocketError();
  this->CloseListener_();
  this->failed_at_ms_ = now_ms;
  this->retry_pending_ = true;
  this->status_set_warning();
}

bool DdpDashboard::OpenListener_(uint32_t now_ms) {
  this->listener_ = socket::socket_loop_monitored(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (!this->listener_) {
    this->ListenerFailed_(now_ms);
    return false;
  }
  // BSDSocketImpl::setblocking does not propagate fcntl errors.
  const int fd = this->listener_->get_fd();
  const int flags = ::fcntl(fd, F_GETFL, 0);
  if (flags == -1 || ::fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1) {
    this->ListenerFailed_(now_ms);
    return false;
  }
  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_ANY);
  address.sin_port = htons(kPort);
  if (this->listener_->bind(reinterpret_cast<sockaddr *>(&address),
                            sizeof(address)) != 0) {
    this->ListenerFailed_(now_ms);
    return false;
  }
  this->retry_pending_ = false;
  this->status_clear_warning();
  return true;
}

void DdpDashboard::loop() {
  this->Service_(millis());
}

void DdpDashboard::Service_(uint32_t now_ms) {
  const uint32_t entry_us = micros();
  if (!this->active_)
    return;
  this->metrics_.RecordLoop(entry_us);
  if (this->frames_ != nullptr && !this->listener_ &&
      (!this->retry_pending_ || uint32_t(now_ms - this->failed_at_ms_) >= kRetryMs))
    this->OpenListener_(now_ms);
  if (this->listener_) {
    const uint32_t started_us = micros();
    bool receive_failed = false;
    for (size_t attempt = 0; attempt < kReadAttempts; ++attempt) {
      if (uint32_t(micros() - started_us) >= kReceiveBudgetUs)
        break;
      // Retry every loop even after a bounded drain; ready() need not report
      // already-buffered packets again. UDP length zero is data, not EOF.
      const ssize_t length = this->listener_->recvfrom(
          this->receive_buffer_.data(), this->receive_buffer_.size(), nullptr, nullptr);
      if (length >= 0) {
        this->metrics_.RecordDatagram(this->frames_->Apply(
            this->receive_buffer_.data(), static_cast<size_t>(length)));
      } else if (errno == EAGAIN || errno == EWOULDBLOCK) {
        break;
      } else if (errno != EINTR) {
        receive_failed = true;
        break;
      }
    }
    this->metrics_.RecordReceivePass(uint32_t(micros() - started_us));
    if (receive_failed)
      this->ListenerFailed_(now_ms);
  }
  if (this->metrics_.IsDue(now_ms))
    this->ReportMetrics_(now_ms);
}

}  // namespace esphome::pixoo64::dashboard
