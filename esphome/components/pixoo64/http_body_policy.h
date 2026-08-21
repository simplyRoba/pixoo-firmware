#pragma once

#include <cstddef>

namespace esphome::pixoo64::http_body {

// A known Content-Length must fit before storage is allocated. Unknown-length
// transfers are bounded while reading instead.
inline bool AcceptAdvertisedSize(size_t advertised_size,
                                 bool has_content_length, size_t max_size) {
  return !has_content_length || advertised_size <= max_size;
}

// A completed fixed-length transfer must contain exactly its advertised body.
// Unknown-length transfers rely on the transport completion signal.
inline bool IsCompleteBody(size_t received_size, size_t advertised_size,
                           bool has_content_length, size_t max_size,
                           bool transfer_complete) {
  return received_size <= max_size && transfer_complete &&
         (!has_content_length || received_size == advertised_size);
}

} // namespace esphome::pixoo64::http_body
