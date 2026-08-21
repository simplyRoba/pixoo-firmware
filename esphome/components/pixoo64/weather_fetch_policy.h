#pragma once

#include <cstddef>

namespace esphome::pixoo64::adapters::weather {

// Current + a 12-hour hourly series (five fields) + daily fits within this
// cap; the request bounds the series length.
constexpr size_t kMaxResponseBytes = 16 * 1024;

} // namespace esphome::pixoo64::adapters::weather
