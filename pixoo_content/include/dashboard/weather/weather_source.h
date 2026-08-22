#pragma once

#include "weather_data.h"

namespace pixoo {

// Abstract weather data source. A consumer (the weather view builder) depends on
// this interface, never on a concrete fetcher; a producer (e.g. an Open-Meteo
// fetcher) implements it. RequestRefresh() is a non-blocking demand signal used
// during preparation and visibility; the implementation controls fetch timing.
// The consumer pulls the latest data via HasData()/Data().
//
// Data() returns by value: an implementation may fetch on another thread, so the
// consumer must receive a coherent snapshot copied under the producer's lock,
// never a reference into state that a background fetch could overwrite mid-read.
class WeatherSource {
 public:
  virtual ~WeatherSource() = default;
  virtual void RequestRefresh() = 0;
  virtual bool HasData() const = 0;
  virtual WeatherData Data() const = 0;
};

}  // namespace pixoo
