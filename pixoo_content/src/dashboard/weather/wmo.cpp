#include "wmo.h"

namespace pixoo {

WeatherCondition WmoToCondition(int wmo_code) {
  switch (wmo_code) {
    case 0:
    case 1:  // clear / mainly clear
      return WeatherCondition::SUNNY;
    case 2:  // partly cloudy
      return WeatherCondition::PARTLYCLOUDY;
    case 3:  // overcast
      return WeatherCondition::CLOUDY;
    case 45:
    case 48:  // fog, depositing rime fog
      return WeatherCondition::FOG;
    case 51:
    case 53:
    case 55:  // drizzle: light / moderate / dense
      return WeatherCondition::DRIZZLE;
    case 56:
    case 57:  // freezing drizzle: light / dense
      return WeatherCondition::FREEZING_DRIZZLE;
    case 61:
    case 63:  // rain: slight / moderate
    case 80:
    case 81:  // rain showers: slight / moderate
      return WeatherCondition::RAINY;
    case 65:  // heavy rain
    case 82:  // violent rain showers
      return WeatherCondition::POURING;
    case 66:
    case 67:  // freezing rain: light / heavy
      return WeatherCondition::FREEZING_RAIN;
    case 71:
    case 73:
    case 75:  // snow: slight / moderate / heavy
    case 85:
    case 86:  // snow showers: slight / heavy
      return WeatherCondition::SNOWY;
    case 77:  // snow grains
      return WeatherCondition::SNOW_GRAINS;
    case 95:  // thunderstorm: slight or moderate
      return WeatherCondition::THUNDERSTORM;
    case 96:
    case 99:  // thunderstorm with slight / heavy hail
      return WeatherCondition::HAIL_THUNDERSTORM;
    default:
      return WeatherCondition::UNKNOWN;
  }
}

}  // namespace pixoo
