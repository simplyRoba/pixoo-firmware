#include "ddp_dashboard.h"

#include <cerrno>
#include <fcntl.h>
#include <new>

#ifdef ESP_PLATFORM
#include "esp_heap_caps.h"
#endif

#include "esphome/core/hal.h"
#include "esphome/core/wake.h"

namespace esphome::pixoo64::dashboard {
namespace {
constexpr uint16_t kPort = 4048;
constexpr uint32_t kRetryMs = 1000;
constexpr uint32_t kReceiveBudgetUs = 1000;
constexpr size_t kReadAttempts = 32;
}  // namespace

void DdpDashboard::HandoffStorageDeleter::operator()(HandoffStorage *storage) const {
#ifdef ESP_PLATFORM
  storage->~HandoffStorage();
  heap_caps_free(storage);
#else
  delete storage;
#endif
}

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
  std::unique_ptr<pixoo::ddp::FrameBuffer, FrameStorageDeleter> frames;
  std::unique_ptr<HandoffStorage, HandoffStorageDeleter> handoff;
#ifdef ESP_PLATFORM
  void *memory = heap_caps_malloc(sizeof(pixoo::ddp::FrameBuffer),
                                 MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (memory != nullptr)
    frames.reset(new (memory) pixoo::ddp::FrameBuffer{});
  if (frames != nullptr) {
    memory = heap_caps_malloc(sizeof(HandoffStorage), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (memory != nullptr)
      handoff.reset(new (memory) HandoffStorage{});
  }
#else
  frames.reset(new (std::nothrow) pixoo::ddp::FrameBuffer{});
  if (frames != nullptr)
    handoff.reset(new (std::nothrow) HandoffStorage{});
#endif
  if (frames == nullptr || handoff == nullptr)
    return false;
  this->frames_ = std::move(frames);
  {
    std::lock_guard<std::mutex> lock(this->handoff_mutex_);
    this->handoff_ = std::move(handoff);
  }
  return true;
}

void DdpDashboard::Render(display::Display &display) const {
  HandoffStorage *handoff;
  {
    std::lock_guard<std::mutex> lock(this->handoff_mutex_);
    handoff = this->handoff_.get();
    if (handoff != nullptr)
      handoff->render_scratch = handoff->published;
  }
  for (size_t y = 0; y < pixoo::ddp::kFrameHeight; ++y) {
    for (size_t x = 0; x < pixoo::ddp::kFrameWidth; ++x) {
      const size_t offset = (y * pixoo::ddp::kFrameWidth + x) * 3;
      display.draw_pixel_at(x, y, handoff == nullptr ? Color(0, 0, 0) :
          Color(handoff->render_scratch[offset], handoff->render_scratch[offset + 1],
                handoff->render_scratch[offset + 2]));
    }
  }
}

void DdpDashboard::OnShow(uint32_t) {
  std::lock_guard<std::mutex> lock(this->handoff_mutex_);
  if (!this->desired_active_) {
    this->desired_active_ = true;
    ++this->generation_;
  }
}

void DdpDashboard::OnHide(uint32_t) {
  std::lock_guard<std::mutex> lock(this->handoff_mutex_);
  if (this->desired_active_) {
    this->desired_active_ = false;
    ++this->generation_;
    if (this->handoff_ != nullptr)
      this->handoff_->published.fill(0);
  }
}

void DdpDashboard::CloseListener_() {
  if (this->listener_) {
    this->listener_->close();
    this->listener_.reset();
  }
}

void DdpDashboard::on_shutdown() {
  this->OnHide(0);
  this->Stop_();
}

void DdpDashboard::Stop_() {
  this->active_ = false;
  this->CloseListener_();
  if (this->frames_ != nullptr)
    this->frames_->Reset();
  this->retry_pending_ = false;
  this->status_clear_warning();
}

void DdpDashboard::ListenerFailed_(uint32_t now_ms) {
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
  bool desired;
  uint32_t generation;
  {
    std::lock_guard<std::mutex> lock(this->handoff_mutex_);
    desired = this->desired_active_;
    generation = this->generation_;
  }
  if (generation != this->applied_generation_) {
    this->Stop_();
    this->applied_generation_ = generation;
    this->active_ = desired;
    if (desired && !this->EnsureFrameStorage_())
      this->status_set_warning();
  }
  if (!this->active_)
    return;
  if (this->frames_ != nullptr && !this->listener_ &&
      (!this->retry_pending_ || uint32_t(now_ms - this->failed_at_ms_) >= kRetryMs))
    this->OpenListener_(now_ms);
  if (this->listener_) {
    const uint32_t started_us = micros();
    bool receive_failed = false;
    bool drain_exhausted = true;
    for (size_t attempt = 0; attempt < kReadAttempts; ++attempt) {
      if (uint32_t(micros() - started_us) >= kReceiveBudgetUs)
        break;
      // ready() need not report already-buffered packets again.
      // UDP length zero is data, not EOF.
      const ssize_t length = this->listener_->recvfrom(
          this->receive_buffer_.data(), this->receive_buffer_.size(), nullptr, nullptr);
      if (length >= 0) {
        if (this->frames_->Apply(this->receive_buffer_.data(),
                                 static_cast<size_t>(length)) == pixoo::ddp::DatagramResult::kPublished) {
          std::lock_guard<std::mutex> lock(this->handoff_mutex_);
          if (this->desired_active_ && this->generation_ == this->applied_generation_)
            this->handoff_->published = this->frames_->frame();
        }
      } else if (errno == EAGAIN || errno == EWOULDBLOCK) {
        drain_exhausted = false;
        break;
      } else if (errno != EINTR) {
        receive_failed = true;
        break;
      }
    }
    if (receive_failed) {
      this->ListenerFailed_(now_ms);
    } else if (drain_exhausted) {
      // Arrival wakes are coalesced; queued packets do not emit a fresh event.
      // Request another bounded pass without waiting for the idle loop interval.
      wake_loop_threadsafe();
    }
  }
}

}  // namespace esphome::pixoo64::dashboard
