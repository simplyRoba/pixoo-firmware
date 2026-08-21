#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "artwork_decoder.h"
#include "http_body_policy.h"

namespace esphome::pixoo64::artwork {

inline bool AcceptBodySize(size_t advertised_size, bool chunked) {
  return http_body::AcceptAdvertisedSize(advertised_size, !chunked,
                                         kMaxEncodedBytes);
}

inline bool IsCompleteBody(size_t received_size, size_t advertised_size,
                           bool chunked, bool transfer_complete) {
  return http_body::IsCompleteBody(received_size, advertised_size, !chunked,
                                   kMaxEncodedBytes, transfer_complete);
}

// Content identity is exact; equality is never inferred from a hash.
inline bool EncodedBodiesEqual(const uint8_t *left, size_t left_size,
                               const uint8_t *right, size_t right_size) {
  return left != nullptr && right != nullptr && left_size == right_size &&
         std::memcmp(left, right, left_size) == 0;
}

constexpr size_t kArtworkSlotCount = 2;

// Reader pins are protected by the caller's publication lock. A writer may
// mutate only a nonpublished slot with no active readers.
inline int8_t SelectWritableSlot(
    int8_t published_slot,
    const uint32_t reader_pins[kArtworkSlotCount]) {
  if (reader_pins == nullptr)
    return -1;
  for (int8_t slot = 0; slot < static_cast<int8_t>(kArtworkSlotCount);
       ++slot) {
    if (slot != published_slot && reader_pins[slot] == 0)
      return slot;
  }
  return -1;
}

// Visibility and generation are deliberately independent of metadata. A
// completion may publish only when both still match its request snapshot.
class FetchPolicy {
 public:
  void SetVisible(bool visible) {
    if (visible_ != visible) {
      visible_ = visible;
      ++generation_;
      if (visible_)
        this->ResetRetry();
    }
  }
  void SetDesired(uint64_t url_identity, bool force_invalidate = false) {
    if (force_invalidate || url_identity != desired_) {
      desired_ = url_identity;
      this->ResetRetry();
      ++generation_;
    }
  }
  uint32_t generation() const { return generation_; }
  bool Accepts(uint32_t request_generation, uint64_t request_identity) const {
    return visible_ && request_generation == generation_ && request_identity == desired_;
  }
  bool ShouldStart(uint32_t now_ms, bool already_complete,
                   bool in_flight) const {
    return visible_ && desired_ != 0 && !already_complete && !in_flight &&
           !retry_exhausted_ &&
           (!retry_pending_ || static_cast<int32_t>(now_ms - next_retry_ms_) >= 0);
  }
  bool Failed(uint32_t now_ms) {
    static constexpr uint32_t kRetryMs[] = {5000, 15000, 60000};
    if (retry_index_ >= sizeof(kRetryMs) / sizeof(kRetryMs[0])) {
      retry_exhausted_ = true;
      return false;
    }
    next_retry_ms_ = now_ms + kRetryMs[retry_index_++];
    retry_pending_ = true;
    return true;
  }
  void Succeeded() {
    next_retry_ms_ = 0;
    retry_index_ = 3;
    retry_pending_ = true;
    retry_exhausted_ = true;
  }
  void ResetRetry() {
    next_retry_ms_ = 0;
    retry_index_ = 0;
    retry_pending_ = false;
    retry_exhausted_ = false;
  }

 private:
  bool visible_{false};
  uint32_t generation_{1};
  uint64_t desired_{0};
  uint32_t next_retry_ms_{0};
  uint8_t retry_index_{0};
  bool retry_pending_{false};
  bool retry_exhausted_{false};
};

}  // namespace esphome::pixoo64::artwork
