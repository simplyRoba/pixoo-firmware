#pragma once

#include "weather_data.h"

namespace pixoo {

// Map an Open-Meteo WMO weather interpretation code to a condition. Day/night
// is not part of the mapping; it travels as a separate is_night flag.
WeatherCondition WmoToCondition(int wmo_code);

}  // namespace pixoo
