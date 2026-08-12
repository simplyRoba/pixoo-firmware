#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>

namespace pixoo {

struct LightState {
  bool on{true};
  float brightness{1.0f};
};

constexpr uint32_t kStopwatchMaximumElapsedMs = 60u * 60u * 1000u;

// Application-owned stopwatch state passed by value to its renderer.
struct StopwatchSnapshot {
  uint32_t elapsed_ms{0};
  bool running{false};
};

constexpr uint32_t kTimerMaximumDurationMs = 100u * 60u * 1000u - 1u;

// Application-owned countdown state passed by value to its renderer.
struct TimerSnapshot {
  uint32_t remaining_ms{0};
  bool running{false};
};

constexpr size_t kMaximumNotificationTextBytes = 256;

// Stores a NUL-terminated byte sequence without allocating. Overlong input is
// retained up to capacity and flagged so the caller can reject it.
class FixedByteString {
 public:
  FixedByteString() = default;
  FixedByteString(const char *value) { this->Assign(value); }
  FixedByteString(const std::string &value) { this->Assign(value); }

  FixedByteString &operator=(const char *value) {
    this->Assign(value);
    return *this;
  }
  FixedByteString &operator=(const std::string &value) {
    this->Assign(value);
    return *this;
  }

  size_t size() const { return this->size_; }
  bool empty() const { return this->size_ == 0; }
  const char *c_str() const { return this->data_; }
  bool overflowed() const { return this->overflowed_; }

 private:
  void Assign(const char *value) {
    if (value == nullptr) {
      this->Assign_({}, 0);
      return;
    }
    this->Assign_(value, std::strlen(value));
  }
  void Assign(const std::string &value) {
    this->Assign_(value.data(), value.size());
  }
  void Assign_(const char *value, size_t size) {
    this->overflowed_ = size > kMaximumNotificationTextBytes;
    this->size_ = this->overflowed_ ? kMaximumNotificationTextBytes : size;
    if (this->size_ != 0)
      std::memcpy(this->data_, value, this->size_);
    this->data_[this->size_] = '\0';
  }

  char data_[kMaximumNotificationTextBytes + 1]{};
  size_t size_{0};
  bool overflowed_{false};
};

// Notification severity. Selects the banner border colour in the renderer.
enum class Severity {
  kInfo,
  kSuccess,
  kWarning,
  kError,
};

struct Notification {
  FixedByteString text;
  Severity severity{Severity::kInfo};
  FixedByteString title{};
};

// Closed vocabulary for renderer-owned reaction animations.
enum class Reaction : uint8_t {
  kLaughing = 0,
  kLove = 1,
  kCrying = 2,
  kAngry = 3,
  kPoop = 4,
  kApprove = 5,
  kDisapprove = 6,
  kCelebrate = 7,
  kThinking = 8,
  kSurprised = 9,
  kFire = 10,
  kEyes = 11,
};

// The tag keeps RenderPort independent of the request that produced an overlay.
enum class OverlayTag {
  kNotification,
  kReaction,
};

struct Overlay {
  OverlayTag tag{OverlayTag::kNotification};
  Notification notification{};
  Reaction reaction{Reaction::kLaughing};
};

// Unknown input falls back to the first canonical value.
Severity ParseSeverity(const std::string &name);
const char *SeverityName(Severity severity);
Reaction ParseReaction(const std::string &name);
const char *ReactionName(Reaction reaction);
uint32_t ReactionVisibleDurationMs(Reaction reaction);

}  // namespace pixoo
