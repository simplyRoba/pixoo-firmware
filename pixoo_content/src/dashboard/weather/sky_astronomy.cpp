#include "sky_astronomy.h"

#include <cmath>

namespace pixoo {
namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kDeg = kPi / 180.0;
constexpr double kSecondsPerDay = 86400.0;

// Half-width of the twilight blend window: dayness ramps over this many seconds
// on each side of sunrise and sunset (about 30 minutes total per event).
constexpr double kTwilightHalf = 900.0;

struct Vec3 {
  double x;
  double y;
  double z;
};

struct MoonPosition {
  bool above_horizon{false};
  double altitude_degrees{0.0};
  double azimuth_degrees{0.0};
  double arc_fraction{0.5};
  double illumination{0.0};
  bool waxing{false};
};

double Clamp(double v, double lo, double hi) {
  return v < lo ? lo : (v > hi ? hi : v);
}

double NormalizeDegrees(double degrees) {
  degrees = std::fmod(degrees, 360.0);
  return degrees < 0.0 ? degrees + 360.0 : degrees;
}

double NormalizeSignedDegrees(double degrees) {
  const double normalized = NormalizeDegrees(degrees);
  return normalized > 180.0 ? normalized - 360.0 : normalized;
}

double SinDegrees(double degrees) { return std::sin(degrees * kDeg); }
double CosDegrees(double degrees) { return std::cos(degrees * kDeg); }

double JulianDay(double epoch_utc) {
  return epoch_utc / kSecondsPerDay + 2440587.5;
}

// Sunrise/sunset for the day containing `epoch_utc`, as UTC epochs, via the
// NOAA sunrise-equation approximation (accurate to about a minute, far finer
// than one 64px arc pixel). has_arc is false when the sun does not cross the
// horizon that day, and sun_up_all_day then distinguishes polar day from night.
struct SunDay {
  bool has_arc{false};
  bool sun_up_all_day{false};  // meaningful only when has_arc is false
  double sunrise{0.0};
  double sunset{0.0};
};

SunDay ComputeSunDay(double epoch_utc, double latitude, double longitude) {
  SunDay out;

  // Select the solar cycle at this longitude. Including longitude here makes
  // the cycle boundary follow local solar time rather than UTC midnight; in
  // particular, +180 and -180 describe the same solar meridian.
  const double n =
      std::round(JulianDay(epoch_utc) - 2451545.0009 + longitude / 360.0);

  // Mean solar noon at this longitude, as days relative to J2000.0.
  const double j_star = n + 0.0009 - longitude / 360.0;

  // Solar mean anomaly.
  const double m = std::fmod(357.5291 + 0.98560028 * j_star, 360.0);
  const double m_rad = m * kDeg;

  // Equation of the center.
  const double c = 1.9148 * std::sin(m_rad) + 0.0200 * std::sin(2 * m_rad) +
                   0.0003 * std::sin(3 * m_rad);

  // Ecliptic longitude.
  const double lambda = std::fmod(m + c + 180.0 + 102.9372, 360.0);
  const double lambda_rad = lambda * kDeg;

  // Solar transit (Julian date of solar noon).
  const double j_transit =
      2451545.0 + j_star + 0.0053 * std::sin(m_rad) -
      0.0069 * std::sin(2 * lambda_rad);

  // Declination of the sun.
  const double sin_decl = std::sin(lambda_rad) * std::sin(23.4397 * kDeg);
  const double cos_decl = std::cos(std::asin(sin_decl));

  const double lat_rad = latitude * kDeg;

  // Hour angle at sunrise/sunset for the standard -0.833 degree horizon.
  const double cos_omega =
      (std::sin(-0.833 * kDeg) - std::sin(lat_rad) * sin_decl) /
      (std::cos(lat_rad) * cos_decl);

  const double solar_noon_epoch = (j_transit - 2440587.5) * kSecondsPerDay;

  if (cos_omega > 1.0) {
    // Sun never rises: polar night.
    out.has_arc = false;
    out.sun_up_all_day = false;
    return out;
  }
  if (cos_omega < -1.0) {
    // Sun never sets: polar day.
    out.has_arc = false;
    out.sun_up_all_day = true;
    return out;
  }

  const double omega = std::acos(cos_omega);
  const double half_day = (omega / (2 * kPi)) * kSecondsPerDay;
  out.has_arc = true;
  out.sunrise = solar_noon_epoch - half_day;
  out.sunset = solar_noon_epoch + half_day;
  return out;
}

// Compact low-precision lunar model from the checked-in Paul Schlyter source.
// The calculation keeps the Moon vector in Earth equatorial radii, then
// subtracts the ellipsoid-adjusted observer vector before deriving local
// horizontal coordinates. This avoids the singularities in the scalar
// topocentric right-ascension/declination correction.
MoonPosition ComputeMoonPosition(double epoch_utc, double latitude,
                                 double longitude) {
  // Schlyter day zero is 2000 Jan 0.0 UT (1999-12-31 00:00 UTC).
  const double d = JulianDay(epoch_utc) - 2451543.5;

  const double moon_node = NormalizeDegrees(125.1228 - 0.0529538083 * d);
  constexpr double moon_inclination = 5.1454;
  const double moon_perigee = NormalizeDegrees(318.0634 + 0.1643573223 * d);
  constexpr double moon_axis = 60.2666;  // Earth equatorial radii
  constexpr double moon_eccentricity = 0.054900;
  const double moon_anomaly =
      NormalizeDegrees(115.3654 + 13.0649929509 * d);

  // Solve E - e*sin(E) = M with the source's approximation as the seed.
  const double mean_anomaly_rad = moon_anomaly * kDeg;
  double eccentric_anomaly =
      mean_anomaly_rad +
      moon_eccentricity * std::sin(mean_anomaly_rad) *
          (1.0 + moon_eccentricity * std::cos(mean_anomaly_rad));
  for (int iteration = 0; iteration < 10; iteration++) {
    const double delta =
        (eccentric_anomaly -
         moon_eccentricity * std::sin(eccentric_anomaly) - mean_anomaly_rad) /
        (1.0 - moon_eccentricity * std::cos(eccentric_anomaly));
    eccentric_anomaly -= delta;
    if (std::fabs(delta) < 1.0e-12)
      break;
  }

  const double xv = moon_axis *
                    (std::cos(eccentric_anomaly) - moon_eccentricity);
  const double yv = moon_axis * std::sqrt(1.0 - moon_eccentricity *
                                                   moon_eccentricity) *
                    std::sin(eccentric_anomaly);
  const double true_anomaly = std::atan2(yv, xv);
  double distance = std::sqrt(xv * xv + yv * yv);

  const double node_rad = moon_node * kDeg;
  const double inclination_rad = moon_inclination * kDeg;
  const double orbit_angle = true_anomaly + moon_perigee * kDeg;
  const double xh =
      distance * (std::cos(node_rad) * std::cos(orbit_angle) -
                  std::sin(node_rad) * std::sin(orbit_angle) *
                      std::cos(inclination_rad));
  const double yh =
      distance * (std::sin(node_rad) * std::cos(orbit_angle) +
                  std::cos(node_rad) * std::sin(orbit_angle) *
                      std::cos(inclination_rad));
  const double zh = distance * std::sin(orbit_angle) *
                    std::sin(inclination_rad);
  double ecliptic_longitude = std::atan2(yh, xh) / kDeg;
  double ecliptic_latitude =
      std::atan2(zh, std::sqrt(xh * xh + yh * yh)) / kDeg;

  const double sun_perigee = NormalizeDegrees(282.9404 + 4.70935e-5 * d);
  const double sun_anomaly = NormalizeDegrees(356.0470 + 0.9856002585 * d);
  const double sun_mean_longitude = sun_anomaly + sun_perigee;
  const double sun_eccentricity = 0.016709 - 1.151e-9 * d;
  const double sun_anomaly_rad = sun_anomaly * kDeg;
  const double sun_eccentric_anomaly =
      sun_anomaly_rad +
      sun_eccentricity * std::sin(sun_anomaly_rad) *
          (1.0 + sun_eccentricity * std::cos(sun_anomaly_rad));
  const double sun_xv =
      std::cos(sun_eccentric_anomaly) - sun_eccentricity;
  const double sun_yv =
      std::sqrt(1.0 - sun_eccentricity * sun_eccentricity) *
      std::sin(sun_eccentric_anomaly);
  const double sun_longitude =
      NormalizeDegrees(std::atan2(sun_yv, sun_xv) / kDeg + sun_perigee);
  const double moon_mean_longitude =
      moon_anomaly + moon_perigee + moon_node;
  const double elongation = moon_mean_longitude - sun_mean_longitude;
  const double argument_latitude = moon_mean_longitude - moon_node;

  // All longitude, latitude, and distance perturbations listed by Schlyter.
  ecliptic_longitude +=
      -1.274 * SinDegrees(moon_anomaly - 2.0 * elongation) +
      0.658 * SinDegrees(2.0 * elongation) -
      0.186 * SinDegrees(sun_anomaly) -
      0.059 * SinDegrees(2.0 * moon_anomaly - 2.0 * elongation) -
      0.057 * SinDegrees(moon_anomaly - 2.0 * elongation + sun_anomaly) +
      0.053 * SinDegrees(moon_anomaly + 2.0 * elongation) +
      0.046 * SinDegrees(2.0 * elongation - sun_anomaly) +
      0.041 * SinDegrees(moon_anomaly - sun_anomaly) -
      0.035 * SinDegrees(elongation) -
      0.031 * SinDegrees(moon_anomaly + sun_anomaly) -
      0.015 * SinDegrees(2.0 * argument_latitude - 2.0 * elongation) +
      0.011 * SinDegrees(moon_anomaly - 4.0 * elongation);
  ecliptic_latitude +=
      -0.173 * SinDegrees(argument_latitude - 2.0 * elongation) -
      0.055 *
          SinDegrees(moon_anomaly - argument_latitude - 2.0 * elongation) -
      0.046 *
          SinDegrees(moon_anomaly + argument_latitude - 2.0 * elongation) +
      0.033 * SinDegrees(argument_latitude + 2.0 * elongation) +
      0.017 * SinDegrees(2.0 * moon_anomaly + argument_latitude);
  distance += -0.58 * CosDegrees(moon_anomaly - 2.0 * elongation) -
              0.46 * CosDegrees(2.0 * elongation);

  const double lon_rad = ecliptic_longitude * kDeg;
  const double lat_rad = ecliptic_latitude * kDeg;
  const Vec3 ecliptic{
      distance * std::cos(lon_rad) * std::cos(lat_rad),
      distance * std::sin(lon_rad) * std::cos(lat_rad),
      distance * std::sin(lat_rad),
  };

  // Ecliptic to equatorial rotation at the epoch of observation.
  const double obliquity = (23.4393 - 3.563e-7 * d) * kDeg;
  const Vec3 geocentric_equatorial{
      ecliptic.x,
      ecliptic.y * std::cos(obliquity) -
          ecliptic.z * std::sin(obliquity),
      ecliptic.y * std::sin(obliquity) +
          ecliptic.z * std::cos(obliquity),
  };

  // Local sidereal time. UT is converted to degrees before being added to the
  // source's GMST0 = solar mean longitude + 180 degrees.
  const double utc_day_fraction =
      std::fmod(epoch_utc, kSecondsPerDay) / kSecondsPerDay;
  const double local_sidereal_degrees = NormalizeDegrees(
      sun_mean_longitude + 180.0 + utc_day_fraction * 360.0 + longitude);
  const double sidereal = local_sidereal_degrees * kDeg;

  // Observer position in the equatorial frame. gclat/rho account for Earth's
  // flattening using the compact correction from the same source.
  const double geodetic_latitude = latitude * kDeg;
  const double geocentric_latitude =
      geodetic_latitude - 0.1924 * kDeg * std::sin(2.0 * geodetic_latitude);
  const double observer_radius =
      0.99833 + 0.00167 * std::cos(2.0 * geodetic_latitude);
  const Vec3 observer{
      observer_radius * std::cos(geocentric_latitude) * std::cos(sidereal),
      observer_radius * std::cos(geocentric_latitude) * std::sin(sidereal),
      observer_radius * std::sin(geocentric_latitude),
  };
  const Vec3 topocentric{
      geocentric_equatorial.x - observer.x,
      geocentric_equatorial.y - observer.y,
      geocentric_equatorial.z - observer.z,
  };

  // Project onto the observer's east, north, and zenith basis. Zenith uses the
  // geodetic latitude, matching a mathematical horizon tangent to the local
  // reference ellipsoid.
  const double sin_lst = std::sin(sidereal);
  const double cos_lst = std::cos(sidereal);
  const double sin_lat = std::sin(geodetic_latitude);
  const double cos_lat = std::cos(geodetic_latitude);
  const double east = -sin_lst * topocentric.x + cos_lst * topocentric.y;
  const double north = -sin_lat * cos_lst * topocentric.x -
                       sin_lat * sin_lst * topocentric.y +
                       cos_lat * topocentric.z;
  const double up = cos_lat * cos_lst * topocentric.x +
                    cos_lat * sin_lst * topocentric.y +
                    sin_lat * topocentric.z;

  MoonPosition out;
  out.altitude_degrees =
      std::atan2(up, std::sqrt(east * east + north * north)) / kDeg;
  out.azimuth_degrees =
      NormalizeDegrees(std::atan2(east, north) / kDeg);
  out.above_horizon = out.altitude_degrees > 0.0;

  // The perturbed Sun-Moon elongation gives phase without assuming a uniform
  // synodic month. East of the Sun is waxing; west is waning.
  const double phase_elongation = std::acos(Clamp(
      CosDegrees(sun_longitude - ecliptic_longitude) *
          std::cos(ecliptic_latitude * kDeg),
      -1.0, 1.0));
  out.illumination = (1.0 - std::cos(phase_elongation)) / 2.0;
  out.waxing = NormalizeDegrees(ecliptic_longitude - sun_longitude) < 180.0;

  // Derive scene progress from topocentric hour angle H. For a crossing body,
  // H=-H0 at rise, zero at transit, and +H0 at set. At latitudes/declinations
  // with no horizon crossing there is no rise-to-set arc, so use its midpoint.
  const double horizontal_distance =
      std::sqrt(topocentric.x * topocentric.x +
                topocentric.y * topocentric.y);
  const double topocentric_declination =
      std::atan2(topocentric.z, horizontal_distance);
  const double topocentric_ra =
      std::atan2(topocentric.y, topocentric.x) / kDeg;
  const double hour_angle =
      NormalizeSignedDegrees(local_sidereal_degrees - topocentric_ra) * kDeg;
  const double crossing_denominator =
      std::cos(geodetic_latitude) * std::cos(topocentric_declination);
  if (std::fabs(crossing_denominator) > 1.0e-12) {
    const double cos_crossing =
        -std::sin(geodetic_latitude) * std::sin(topocentric_declination) /
        crossing_denominator;
    if (cos_crossing >= -1.0 && cos_crossing <= 1.0) {
      const double crossing_hour_angle = std::acos(cos_crossing);
      if (crossing_hour_angle > 1.0e-12) {
        out.arc_fraction = Clamp(
            (hour_angle + crossing_hour_angle) /
                (2.0 * crossing_hour_angle),
            0.0, 1.0);
      }
    }
  }
  return out;
}

}  // namespace

SkyState ComputeSkyState(std::time_t epoch_utc, float latitude,
                         float longitude) {
  SkyState s;
  if (epoch_utc <= 0 || !std::isfinite(latitude) ||
      !std::isfinite(longitude) || latitude < -90.0f || latitude > 90.0f ||
      longitude < -180.0f || longitude > 180.0f)
    return s;

  const double epoch = static_cast<double>(epoch_utc);

  const SunDay day = ComputeSunDay(epoch, latitude, longitude);
  if (day.has_arc) {
    s.has_arc = true;
    s.is_daytime = epoch >= day.sunrise && epoch <= day.sunset;
    const double span = day.sunset - day.sunrise;
    if (span > 0.0)
      s.day_fraction =
          static_cast<float>(Clamp((epoch - day.sunrise) / span, 0.0, 1.0));
    // dayness ramps 0->1 across a window centred on sunrise and 1->0 across one
    // centred on sunset, and is flat 1 in between and 0 outside.
    const double sunrise_up = Clamp(
        (epoch - (day.sunrise - kTwilightHalf)) / (2 * kTwilightHalf), 0.0, 1.0);
    const double sunset_down = Clamp(
        ((day.sunset + kTwilightHalf) - epoch) / (2 * kTwilightHalf), 0.0, 1.0);
    s.dayness = static_cast<float>(sunrise_up * sunset_down);
  } else {
    s.has_arc = false;
    s.is_daytime = day.sun_up_all_day;
    s.day_fraction = 0.5f;
    s.dayness = day.sun_up_all_day ? 1.0f : 0.0f;
  }

  const MoonPosition moon =
      ComputeMoonPosition(epoch, latitude, longitude);
  s.moon_above_horizon = moon.above_horizon;
  s.moon_altitude_degrees = static_cast<float>(moon.altitude_degrees);
  s.moon_azimuth_degrees = static_cast<float>(moon.azimuth_degrees);
  s.moon_arc_fraction = static_cast<float>(moon.arc_fraction);
  s.moon_illumination = static_cast<float>(moon.illumination);
  s.moon_waxing = moon.waxing;

  s.valid = true;
  return s;
}

}  // namespace pixoo
