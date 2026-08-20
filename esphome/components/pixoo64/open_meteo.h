#pragma once

#include <cstdint>

#include "esphome/core/component.h"
#include "weather_source.h"

namespace esphome::http_request {
class HttpRequestComponent;
}  // namespace esphome::http_request

namespace esphome::number {
class Number;
}  // namespace esphome::number

namespace esphome::pixoo64::adapters {

class HttpRequestGate;

// Fetches current + hourly + daily weather from the Open-Meteo forecast API and
// exposes it through the WeatherSource interface. RequestRefresh() is called by
// the weather dashboard while its weather is being shown; it fetches only when
// the data is stale (and the network is up), so nothing is fetched while the
// dashboard is hidden.
//
// The fetch (HTTP GET + JSON parse) runs on a background task, so it never
// blocks the render loop. The worker hands completion to the render task, which
// applies refresh policy and publishes accepted WeatherData snapshots. All
// FreeRTOS state is held behind an opaque impl pointer so this header stays
// usable in the host render-test build, which has no FreeRTOS.
class OpenMeteoSource : public Component, public pixoo::WeatherSource {
 public:
  OpenMeteoSource();
  ~OpenMeteoSource();

  void setup() override;
  void on_shutdown() override;
  bool teardown() override;
  float get_setup_priority() const override {
    return setup_priority::AFTER_WIFI - 1.0f;
  }

  void set_http_request(http_request::HttpRequestComponent *r) { this->http_ = r; }
  void set_http_request_gate(HttpRequestGate *gate) { this->http_gate_ = gate; }
  // Location and refresh cadence are backed by persisted number entities (NVS),
  // not compiled-in constants, so they change without a reflash. The render
  // task snapshots location before each background fetch.
  void set_latitude(number::Number *n) { this->latitude_ = n; }
  void set_longitude(number::Number *n) { this->longitude_ = n; }
  // Number entity holding the minimum time between fetches, in minutes.
  void set_refresh_interval(number::Number *n) { this->refresh_interval_ = n; }

  void RequestRefresh() override;
  bool HasData() const override;
  pixoo::WeatherData Data() const override;

 protected:
  // Runs on the background task: performs the blocking fetch + parse and
  // reports a completion. Snapshot publication and policy mutation stay on the
  // render task.
  void FetchJob_();
  void ConsumeCompletion_(uint32_t now);
  // The render task compares persisted location values on demand. The first
  // observation establishes a baseline; later changes invalidate before a
  // request is begun.
  void ObserveLocation_(float latitude, float longitude);
  bool fetch_(pixoo::WeatherData *out, float latitude, float longitude,
              uint32_t generation);

  // Current refresh interval in ms, read from the refresh_interval_ number
  // entity (minutes) each RequestRefresh(); falls back to 30 min if unset.
  uint32_t refresh_interval_ms_() const;

  http_request::HttpRequestComponent *http_{nullptr};
  HttpRequestGate *http_gate_{nullptr};
  number::Number *latitude_{nullptr};
  number::Number *longitude_{nullptr};
  number::Number *refresh_interval_{nullptr};
  bool location_observed_{false};
  float observed_latitude_{0.0f};
  float observed_longitude_{0.0f};

  // Opaque holder for the background worker and the thread-safe result buffer
  // (defined in the .cpp so FreeRTOS stays out of this header).
  struct Impl;
  Impl *impl_{nullptr};
};

}  // namespace esphome::pixoo64::adapters
