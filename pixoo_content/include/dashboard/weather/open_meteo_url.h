#pragma once

#include <string>

namespace pixoo {

// Builds the Open-Meteo forecast URL used by the weather source. Coordinates
// are serialized to four decimal places, matching the device configuration.
std::string BuildOpenMeteoForecastUrl(float latitude, float longitude);

}  // namespace pixoo
