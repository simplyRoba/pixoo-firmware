#pragma once

#include <cstdint>
#include <string>
#include <utility>

#include "esphome/components/display/display.h"

namespace pixoo {
class EqualizerLevelsSink;
}  // namespace pixoo

namespace esphome::pixoo64::dashboard {

class Dashboard {
 public:
  virtual ~Dashboard() = default;
  void set_id(std::string id) { this->id_ = std::move(id); }
  const std::string &id() const { return this->id_; }
  void set_frame_interval_ms(uint32_t interval_ms) {
    this->frame_interval_ms_ = interval_ms;
  }
  uint32_t frame_interval_ms() const { return this->frame_interval_ms_; }
  virtual bool available() const = 0;
  virtual bool requires_microphone() const { return false; }
  // Non-null for a dashboard that consumes microphone spectrum levels, letting
  // the renderer fan captured levels out to every such dashboard.
  virtual pixoo::EqualizerLevelsSink *levels_sink() { return nullptr; }
  virtual void Render(display::Display &display) const = 0;
  virtual void Prepare(uint32_t now_ms) { (void) now_ms; }
  virtual void CancelPreparation(uint32_t now_ms) { (void) now_ms; }
  virtual bool ReadyToShow() const { return this->available(); }
  virtual bool HasPresentation() const { return this->available(); }
  // Called on the visible dashboard each render tick, before Render(). Lets a
  // dashboard signal demand for data (e.g. request a weather refresh) and
  // advance animation state to `now_ms`; Render() is const.
  virtual void Tick(uint32_t now_ms) { (void) now_ms; }
  // Called once when this dashboard becomes the visible one, before its first
  // Tick(). Lets a dashboard restart a presentation it only plays on entry.
  virtual void OnShow(uint32_t now_ms) { (void) now_ms; }
  // Called once when this dashboard stops being visible. A dashboard may use
  // this to cancel visibility-scoped background work without affecting data
  // subscriptions that remain active for the application lifetime.
  virtual void OnHide(uint32_t now_ms) { (void) now_ms; }

 protected:
  std::string id_;
  uint32_t frame_interval_ms_{33};
};

}  // namespace esphome::pixoo64::dashboard
