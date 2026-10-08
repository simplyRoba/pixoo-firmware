#pragma once

#include <array>
#include <memory>

#include "dashboard.h"
#include "ddp_frame.h"
#include "esphome/components/socket/socket.h"
#include "esphome/core/component.h"

namespace esphome::pixoo64::dashboard {

class DdpDashboard : public Dashboard, public Component {
 public:
  bool available() const override { return true; }
  void Render(display::Display &display) const override;
  void OnShow(uint32_t now_ms) override;
  void OnHide(uint32_t now_ms) override;
  void loop() override;
  void on_shutdown() override;
  bool active() const { return this->active_; }

 protected:
  void Service_(uint32_t now_ms);
  void Stop_();
  std::unique_ptr<socket::Socket> listener_;

 private:
  struct FrameStorageDeleter {
    void operator()(pixoo::ddp::FrameBuffer *storage) const;
  };
  bool EnsureFrameStorage_();
  bool OpenListener_(uint32_t now_ms);
  void ListenerFailed_(uint32_t now_ms);
  void CloseListener_();
  // Visibility, socket reads, and frame publication share the ESPHome main loop.
  bool active_{false};
  bool retry_pending_{false};
  uint32_t failed_at_ms_{0};
  bool allocation_attempted_{false};
  std::unique_ptr<pixoo::ddp::FrameBuffer, FrameStorageDeleter> frames_;
  std::array<uint8_t, pixoo::ddp::kMaxDatagramBytes + 1> receive_buffer_{};
};

}  // namespace esphome::pixoo64::dashboard
