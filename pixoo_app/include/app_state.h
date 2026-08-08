#pragma once

#include <cstdint>
#include <string>

namespace pixoo {

struct LightState {
  bool on{true};
  float brightness{1.0f};
};

// Notification severity. Selects the banner border colour in the renderer.
enum class Severity {
  kInfo,
  kSuccess,
  kWarning,
  kError,
};

struct Notification {
  std::string text;
  Severity severity{Severity::kInfo};
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
