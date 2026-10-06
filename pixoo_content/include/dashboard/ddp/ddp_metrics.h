#pragma once

#include <algorithm>
#include <cstdint>

#include "ddp_frame.h"

namespace pixoo::ddp {

struct MetricsSnapshot {
  uint32_t elapsed_ms{0};
  uint32_t received{0};
  uint32_t rejected{0};
  uint32_t publications{0};
  uint32_t rendered_revisions{0};
  uint64_t latest_revision{0};
  uint32_t receive_passes{0};
  uint64_t receive_total_us{0};
  float receive_average_us{0};
  uint32_t receive_max_us{0};
  uint32_t loop_gap_max_us{0};
  uint32_t socket_errors{0};
};

// Caller supplies clocks; unsigned subtraction handles uint32 timestamp wrap.
// PUSH counts publications, not complete frames. Render counts distinct latest
// published revisions drawn by the dashboard, not frames presented by a panel.
class MetricsWindow {
 public:
  static constexpr uint32_t kWindowMs = 5000;

  void Reset(uint32_t now_ms) {
    started_ = true;
    started_ms_ = now_ms;
    numbers_ = {};
    latest_revision_ = last_drawn_revision_ = 0;
    have_loop_ = false;
    last_loop_us_ = 0;
  }

  void RecordLoop(uint32_t now_us) {
    if (!started_) return;
    if (have_loop_)
      numbers_.loop_gap_max_us =
          std::max(numbers_.loop_gap_max_us, uint32_t(now_us - last_loop_us_));
    last_loop_us_ = now_us;
    have_loop_ = true;
  }

  void RecordDatagram(DatagramResult result) {
    if (!started_) return;
    ++numbers_.received;
    if (result == DatagramResult::kRejected) ++numbers_.rejected;
    if (result == DatagramResult::kPublished) {
      ++numbers_.publications;
      ++latest_revision_;
    }
  }

  void RecordRender(bool has_frame) {
    if (!started_ || !has_frame || latest_revision_ == last_drawn_revision_) return;
    ++numbers_.rendered_revisions;
    last_drawn_revision_ = latest_revision_;
  }

  // Includes an empty EAGAIN receive attempt; socket setup/retry and reporting
  // are outside this duration. No receive pass exists without a listener.
  void RecordReceivePass(uint32_t duration_us) {
    if (!started_) return;
    ++numbers_.receive_passes;
    numbers_.receive_total_us += duration_us;
    numbers_.receive_max_us = std::max(numbers_.receive_max_us, duration_us);
  }

  void RecordSocketError() { if (started_) ++numbers_.socket_errors; }

  bool IsDue(uint32_t now_ms) const {
    return started_ && uint32_t(now_ms - started_ms_) >= kWindowMs;
  }

  MetricsSnapshot Snapshot(uint32_t now_ms) const {
    auto result = numbers_;
    result.elapsed_ms = started_ ? uint32_t(now_ms - started_ms_) : 0;
    result.latest_revision = latest_revision_;
    result.receive_average_us = result.receive_passes == 0 ? 0.0f :
        static_cast<float>(result.receive_total_us) / result.receive_passes;
    return result;
  }

  bool Close(uint32_t now_ms, MetricsSnapshot *snapshot) {
    if (!started_ || snapshot == nullptr || uint32_t(now_ms - started_ms_) == 0)
      return false;
    *snapshot = Snapshot(now_ms);
    numbers_ = {};
    started_ms_ = now_ms;
    // Revision identity and the preceding active loop survive window boundaries.
    return true;
  }

 private:
  bool started_{false};
  uint32_t started_ms_{0};
  MetricsSnapshot numbers_{};
  uint64_t latest_revision_{0};
  uint64_t last_drawn_revision_{0};
  bool have_loop_{false};
  uint32_t last_loop_us_{0};
};

}  // namespace pixoo::ddp
