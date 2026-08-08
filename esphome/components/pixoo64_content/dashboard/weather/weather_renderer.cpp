#include "weather_renderer.h"

#include <cstdio>

namespace esphome::pixoo64::weather {
namespace {

const Color kBlack(0, 0, 0);
const Color kWhite(235, 238, 245);
const Color kDim(120, 126, 140);
const Color kHigh(240, 120, 70);   // warm accent for daily high
const Color kLow(90, 150, 240);    // cool accent for daily low
const Color kDrop(90, 170, 245);   // humidity accent
const Color kDivider(40, 44, 54);

// Color the hero temperature subtly by how warm it is: cold->cool white-blue,
// warm->warm amber. Kept desaturated so it reads as near-white.
Color TempColor(float t) {
  if (t <= 0.0f)
    return Color(200, 214, 245);
  if (t >= 30.0f)
    return Color(245, 205, 170);
  return kWhite;
}

void DrawHumidity(display::Display &d, int x, int y, const WeatherViewModel &v,
                  font::Font *font) {
  // The trailing '%' already marks this as humidity, so no glyph is needed.
  char buf[8];
  std::snprintf(buf, sizeof(buf), "%.0f%%", v.humidity);
  d.print(x, y, font, kDrop, display::TextAlign::TOP_RIGHT, buf);
}

void DrawForecastStrip(display::Display &d, const WeatherViewModel &v,
                       const WeatherFonts &fonts, int top) {
  int cols = 0;
  for (int i = 0; i < kForecastHours; i++)
    if (v.hours[i].valid)
      cols++;
  if (cols == 0)
    return;

  // Leave a 1px gap between the divider and the hour labels below it.
  d.horizontal_line(0, top - 2, 64, kDivider);
  const int cell = 64 / cols;
  for (int i = 0; i < cols; i++) {
    const WeatherHour &h = v.hours[i];
    const int cx = i * cell + cell / 2;
    if (i > 0)
      d.vertical_line(i * cell, top + 1, 64 - top - 2, kDivider);
    // Three ~21px columns leave room for a divider plus the widest temperature
    // ("-20\xC2\xB0"). The temperature's degree sign is 4px wide, so the hour
    // right-aligns to the temperature's DIGITS (one glyph left of the temp's
    // right edge) and the digits stack vertically.
    const int tempRight = i * cell + cell - 2;  // temp right edge, 1px margin
    const int digitRight = tempRight - 4;       // drop the degree glyph width
    d.print(digitRight, top, fonts.small, kDim, display::TextAlign::TOP_RIGHT,
            h.label.c_str());
    DrawWeatherIconMini(d, h.condition, h.is_night, cx, top + 11, 4);
    char buf[8];
    std::snprintf(buf, sizeof(buf), "%d\xC2\xB0",
                  pixoo::DisplayDegrees(h.temperature));
    d.print(tempRight, top + 18, fonts.small, kWhite,
            display::TextAlign::TOP_RIGHT, buf);
  }
}

}  // namespace

void RenderWeather(display::Display &d, const WeatherViewModel &v,
                       const WeatherFonts &fonts, const IconAnimation &anim) {
  d.fill(kBlack);

  // Top strip: clock (left) and date (right).
  if (!v.time_text.empty())
    d.print(1, 1, fonts.small, kWhite, display::TextAlign::TOP_LEFT,
            v.time_text.c_str());
  if (!v.date_text.empty())
    d.print(63, 1, fonts.small, kDim, display::TextAlign::TOP_RIGHT,
            v.date_text.c_str());

  // Hero row: large condition icon on the left, big temperature on the right.
  const int heroTop = 7;
  // The icon keeps clear of the clock above it and the statistics below by the
  // same margin, so it never crowds either: its box is [cy - r, cy + r] and
  // both rows of text stay outside it.
  DrawWeatherIconHero(d, v.condition, v.is_night, 14, 18, 10, anim);

  if (v.has_temperature) {
    char buf[8];
    std::snprintf(buf, sizeof(buf), "%d", pixoo::DisplayDegrees(v.temperature));
    const int heroRight = 57;
    d.print(heroRight, heroTop - 1, fonts.big, TempColor(v.temperature),
            display::TextAlign::TOP_RIGHT, buf);
    // Degree mark in the small font. The big font has empty headroom above the
    // glyphs, so offset the mark down to sit at the top of the actual digits
    // (superscript) rather than floating in that headroom.
    d.print(heroRight + 2, heroTop + 3, fonts.small, kDim,
            display::TextAlign::TOP_LEFT, "\xC2\xB0");
  }

  // Feels-like sits under the hero temperature, right-aligned to the panel.
  if (v.has_feels_like) {
    char buf[16];
    std::snprintf(buf, sizeof(buf), "~%d\xC2\xB0",
                  pixoo::DisplayDegrees(v.feels_like));
    d.print(62, heroTop + 16, fonts.small, kWhite, display::TextAlign::TOP_RIGHT,
            buf);
  }

  // Secondary stats row: high/low as one group on the left, humidity right.
  int sy = 31;
  if (v.has_high) {
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%d\xC2\xB0",
                  pixoo::DisplayDegrees(v.high));
    d.print(1, sy, fonts.small, kHigh, display::TextAlign::TOP_LEFT, buf);
  }
  if (v.has_low) {
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%d\xC2\xB0",
                  pixoo::DisplayDegrees(v.low));
    // Start past the widest possible high ("-24\xC2\xB0" = 16px from x=1) plus a
    // gap, so a negative high never runs into the low.
    d.print(20, sy, fonts.small, kLow, display::TextAlign::TOP_LEFT, buf);
  }
  if (v.has_humidity)
    DrawHumidity(d, 62, sy, v, fonts.small);

  DrawForecastStrip(d, v, fonts, 40);
}

void RenderWeatherLoading(display::Display &d, const WeatherFonts &fonts) {
  d.fill(kBlack);
  if (fonts.small != nullptr)
    d.print(32, 30, fonts.small, kDim, display::TextAlign::CENTER, "...");
}

}  // namespace esphome::pixoo64::weather
