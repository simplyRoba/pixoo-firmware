#pragma once

#include "weather_data.h"

namespace pixoo {

struct WeatherSnapshot {
  WeatherData data{};
  bool available{false};
};

// Abstract weather data source. A consumer (the weather view builder) depends on
// this interface, never on a concrete fetcher; a producer (e.g. an Open-Meteo
// fetcher) implements it. RequestRefresh() is a non-blocking demand signal used
// during preparation and visibility; the implementation controls fetch timing.
// Consumers use ReadSnapshot() to obtain availability and data together.
// Concurrent producers override it to return a coherent copy.
class WeatherSource {
 public:
  virtual ~WeatherSource() = default;
  virtual void RequestRefresh() = 0;
  virtual bool HasData() const = 0;
  virtual WeatherData Data() const = 0;
  virtual WeatherSnapshot ReadSnapshot() const { return {this->Data(), this->HasData()}; }
};

}  // namespace pixoo
