#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <type_traits>

namespace esphome::pixoo64::now_playing_config {

constexpr uint8_t kConfigFormatVersion = 2;
constexpr uint8_t kConfigValidMarker = 0xa7;
constexpr size_t kMaxEntityIdBytes = 96;
constexpr size_t kMaxHomeAssistantUrlBytes = 256;
constexpr size_t kMaxResolvedArtworkUrlBytes = 768;
constexpr uint32_t kPreferenceKey = 0x4e504331u;

struct ConfigRecord {
  uint8_t format_version{0};
  uint8_t valid_marker{0};
  uint16_t entity_id_size{0};
  uint16_t home_assistant_url_size{0};
  uint32_t revision{0};
  char entity_id[kMaxEntityIdBytes + 1]{};
  char home_assistant_url[kMaxHomeAssistantUrlBytes + 1]{};
};

static_assert(std::is_trivially_copyable<ConfigRecord>::value,
              "configuration record must be preference-safe");

bool ValidateEntityId(const char *value, size_t size);
bool ValidateConfigRecord(const ConfigRecord &record);
bool MakeConfigRecord(const char *entity_id, size_t entity_id_size,
                      const char *home_assistant_url,
                      size_t home_assistant_url_size, uint32_t revision,
                      ConfigRecord *record);

// Validates an absolute Home Assistant HTTP(S) URL and removes only trailing
// path slashes. The result has no query or fragment.
bool NormalizeHomeAssistantUrl(const char *value, size_t size,
                               std::string *normalized);

// Resolves a validated Home Assistant entity_picture URL. Absolute HTTP(S)
// values retain their query, while one-slash relative paths append to base_url.
bool ResolveArtworkUrl(const char *base_url, size_t base_url_size,
                       const char *entity_picture, size_t entity_picture_size,
                       std::string *resolved);

// Removes query values before a URL is used in diagnostics.
std::string RedactUrlForDiagnostics(const char *value, size_t size);

}  // namespace esphome::pixoo64::now_playing_config
