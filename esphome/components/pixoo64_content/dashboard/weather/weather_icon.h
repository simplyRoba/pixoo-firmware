#pragma once

#include <cstdint>

#include "esphome/components/display/display.h"
#include "wmo.h"

namespace esphome::pixoo64::weather {

using pixoo::WeatherCondition;

// How far an icon is through its loop. Every icon animates as a pure function
// of this time, so a frame depends on nothing a renderer has to retain, and the
// same time always draws the same frame. Motion is periodic and continuous
// across each period boundary, so a loop never jumps.
struct IconAnimation {
  uint64_t elapsed_ms{0};
  // Brightness of a lightning event, 0 outside one. Only the storm icons use
  // it, and they hold their bolt visible between events.
  float lightning{0.0f};
};

// Large, gradient-shaded hero icon centered at [cx,cy]. `size` is the box
// half-extent; the icon fills roughly a (2*size) square. Detailed and colorful,
// anti-aliased from each shape's coverage of a pixel, and animated by `anim`.
// `is_night` swaps the sun for a moon in the conditions that show one; the
// others are identical day and night.
void DrawWeatherIconHero(display::Display &display, WeatherCondition condition,
                         bool is_night, int cx, int cy, int size,
                         const IconAnimation &anim = {});

// Small, crisp forecast icon centered at [cx,cy], purpose-drawn for ~9-11px
// (not a shrunk hero). High contrast, minimal detail, and static: the strip
// reads as data next to the animated hero. Round bodies are anti-aliased; bars,
// dots, and streaks stay on whole pixels so the small art keeps its edge.
void DrawWeatherIconMini(display::Display &display, WeatherCondition condition,
                         bool is_night, int cx, int cy, int size);

}  // namespace esphome::pixoo64::weather
