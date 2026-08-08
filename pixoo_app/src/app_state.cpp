#include "app_state.h"

namespace pixoo {

Severity ParseSeverity(const std::string &name) {
  if (name == "success")
    return Severity::kSuccess;
  if (name == "warning")
    return Severity::kWarning;
  if (name == "error")
    return Severity::kError;
  return Severity::kInfo;
}

const char *SeverityName(Severity severity) {
  switch (severity) {
    case Severity::kSuccess:
      return "success";
    case Severity::kWarning:
      return "warning";
    case Severity::kError:
      return "error";
    case Severity::kInfo:
      break;
  }
  return "info";
}

Reaction ParseReaction(const std::string &name) {
  if (name == "love")
    return Reaction::kLove;
  if (name == "crying")
    return Reaction::kCrying;
  if (name == "angry")
    return Reaction::kAngry;
  if (name == "poop")
    return Reaction::kPoop;
  if (name == "approve")
    return Reaction::kApprove;
  if (name == "disapprove")
    return Reaction::kDisapprove;
  if (name == "celebrate")
    return Reaction::kCelebrate;
  if (name == "thinking")
    return Reaction::kThinking;
  if (name == "surprised")
    return Reaction::kSurprised;
  if (name == "fire")
    return Reaction::kFire;
  if (name == "eyes")
    return Reaction::kEyes;
  return Reaction::kLaughing;
}

uint32_t ReactionVisibleDurationMs(Reaction reaction) {
  switch (reaction) {
    case Reaction::kApprove:
    case Reaction::kDisapprove:
      return 1500;
    case Reaction::kAngry:
    case Reaction::kSurprised:
      return 1700;
    case Reaction::kLaughing:
    case Reaction::kPoop:
    case Reaction::kEyes:
      return 1800;
    case Reaction::kLove:
    case Reaction::kThinking:
    case Reaction::kFire:
      return 2000;
    case Reaction::kCrying:
    case Reaction::kCelebrate:
      return 2200;
  }
  return 1800;
}

const char *ReactionName(Reaction reaction) {
  switch (reaction) {
    case Reaction::kLove:
      return "love";
    case Reaction::kCrying:
      return "crying";
    case Reaction::kAngry:
      return "angry";
    case Reaction::kPoop:
      return "poop";
    case Reaction::kApprove:
      return "approve";
    case Reaction::kDisapprove:
      return "disapprove";
    case Reaction::kCelebrate:
      return "celebrate";
    case Reaction::kThinking:
      return "thinking";
    case Reaction::kSurprised:
      return "surprised";
    case Reaction::kFire:
      return "fire";
    case Reaction::kEyes:
      return "eyes";
    case Reaction::kLaughing:
      break;
  }
  return "laughing";
}

}  // namespace pixoo
