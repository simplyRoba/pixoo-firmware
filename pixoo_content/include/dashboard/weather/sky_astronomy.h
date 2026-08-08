#pragma once

#include <ctime>

namespace pixoo {

// Derived sky state for the sun/moon presentation, computed from a UTC instant
// and a location. All fields are source-independent: they come from astronomy,
// not from a weather fetch. Everything is derived from the UTC epoch, so the
// result is timezone-independent and deterministic for a given instant and
// location.
struct SkyState {
  bool valid{false};  // false when the instant or location is unusable

  // Sun. is_daytime is true between sunrise and sunset. day_fraction runs 0 at
  // sunrise to 1 at sunset (the sun's position along its arc); it is only
  // meaningful while is_daytime. has_arc is false at the poles when the sun
  // neither rises nor sets on this day, in which case is_daytime still reports
  // whether the sun is up (polar day) or down (polar night).
  bool is_daytime{false};
  bool has_arc{false};
  float day_fraction{0.0f};

  // Smooth day/night blend for the twilight transition: 0 deep night, 1 full
  // day, ramping across a window around sunrise and sunset. Drives every
  // cross-fade (sky, light, stars) and daylight attenuation, so day and night
  // hand over gradually rather than switching in one frame.
  float dayness{0.0f};

  // Moon. Position is topocentric (as seen from this location): altitude is
  // degrees above the mathematical horizon, and azimuth is clockwise from
  // north (north=0, east=90). arc_fraction maps the current rise-to-set path
  // onto a generic east-left/west-right scene: 0 at rising, about 0.5 at
  // transit, and 1 at setting. Circumpolar and never-rising cases use 0.5.
  bool moon_above_horizon{false};
  float moon_altitude_degrees{0.0f};
  float moon_azimuth_degrees{0.0f};
  float moon_arc_fraction{0.0f};

  // illumination is the lit fraction of the disc, 0 at new moon to 1 at full.
  // waxing is true while it is growing (lit on the trailing side), false while
  // waning.
  float moon_illumination{0.0f};
  bool moon_waxing{false};
};

// Compute the sky state for a UTC epoch and a location. latitude is degrees
// north (-90..90), longitude degrees east (-180..180). A zero or out-of-range
// epoch yields an invalid state.
SkyState ComputeSkyState(std::time_t epoch_utc, float latitude,
                         float longitude);

}  // namespace pixoo
