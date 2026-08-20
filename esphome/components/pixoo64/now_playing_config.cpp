#include "now_playing_config.h"

#include <cstring>

namespace esphome::pixoo64::now_playing_config {
namespace {

bool IsControlOrWhitespace(unsigned char c) { return c <= 0x20 || c == 0x7f; }
bool IsAlphaNum(unsigned char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
         (c >= '0' && c <= '9');
}
bool IsHex(unsigned char c) {
  return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
         (c >= 'A' && c <= 'F');
}

bool HasUnsafeByte(const char *value, size_t size) {
  if (value == nullptr) return true;
  for (size_t i = 0; i < size; ++i)
    if (IsControlOrWhitespace(static_cast<unsigned char>(value[i]))) return true;
  return false;
}

bool ValidDnsName(const char *value, size_t size) {
  if (size == 0 || size > 253) return false;
  size_t label_size = 0;
  for (size_t i = 0; i < size; ++i) {
    const unsigned char c = value[i];
    if (c == '.') {
      if (label_size == 0 || value[i - 1] == '-') return false;
      label_size = 0;
    } else if (IsAlphaNum(c) || c == '-') {
      if (label_size == 0 && c == '-') return false;
      if (++label_size > 63) return false;
    } else {
      return false;
    }
  }
  return label_size != 0 && value[size - 1] != '-';
}

bool ValidIpv6(const char *value, size_t size) {
  if (size < 2) return false;
  size_t offset = 0;
  size_t groups = 0;
  bool compressed = false;
  while (offset < size) {
    if (value[offset] == ':') {
      if (compressed || offset + 1 >= size || value[offset + 1] != ':') return false;
      compressed = true;
      offset += 2;
      if (offset == size) break;
      continue;
    }
    const size_t start = offset;
    while (offset < size && value[offset] != ':') {
      if (!IsHex(static_cast<unsigned char>(value[offset]))) return false;
      ++offset;
    }
    if (offset - start == 0 || offset - start > 4 || ++groups > 8) return false;
    if (offset == size) break;
    if (offset + 1 < size && value[offset + 1] == ':') continue;
    if (++offset == size) return false;
  }
  return compressed ? groups < 8 : groups == 8;
}

bool ValidPort(const char *value, size_t size) {
  if (size == 0 || size > 5) return false;
  uint32_t port = 0;
  for (size_t i = 0; i < size; ++i) {
    if (value[i] < '0' || value[i] > '9') return false;
    port = port * 10 + static_cast<uint32_t>(value[i] - '0');
  }
  return port >= 1 && port <= 65535;
}

struct ParsedUrl {
  size_t authority_end{0};
  size_t path_end{0};
  size_t query_start{0};
  bool has_query{false};
};

bool ParseAbsoluteHttpUrl(const char *value, size_t size, bool allow_query,
                          ParsedUrl *parsed) {
  if (value == nullptr || parsed == nullptr || size == 0 || HasUnsafeByte(value, size))
    return false;
  const char *scheme_end = nullptr;
  if (size >= 7 && std::memcmp(value, "http://", 7) == 0) scheme_end = value + 7;
  if (size >= 8 && std::memcmp(value, "https://", 8) == 0) scheme_end = value + 8;
  if (scheme_end == nullptr) return false;
  const size_t authority_start = static_cast<size_t>(scheme_end - value);
  size_t authority_end = authority_start;
  while (authority_end < size && value[authority_end] != '/' &&
         value[authority_end] != '?' && value[authority_end] != '#')
    ++authority_end;
  if (authority_end == authority_start) return false;
  for (size_t i = authority_start; i < authority_end; ++i)
    if (value[i] == '@') return false;

  size_t host_start = authority_start;
  size_t host_end = authority_end;
  if (value[host_start] == '[') {
    size_t close = host_start + 1;
    while (close < authority_end && value[close] != ']') ++close;
    if (close == authority_end || !ValidIpv6(value + host_start + 1, close - host_start - 1))
      return false;
    host_end = close + 1;
    if (host_end < authority_end &&
        (value[host_end] != ':' || !ValidPort(value + host_end + 1, authority_end - host_end - 1)))
      return false;
  } else {
    size_t colon = authority_end;
    for (size_t i = host_start; i < authority_end; ++i)
      if (value[i] == ':') {
        if (colon != authority_end) return false;
        colon = i;
      }
    host_end = colon;
    if (!ValidDnsName(value + host_start, host_end - host_start)) return false;
    if (colon != authority_end &&
        !ValidPort(value + colon + 1, authority_end - colon - 1)) return false;
  }

  size_t query_start = size;
  for (size_t i = authority_end; i < size; ++i) {
    if (value[i] == '#') return false;
    if (value[i] == '?') {
      if (!allow_query || query_start != size) return false;
      query_start = i;
    }
  }
  parsed->authority_end = authority_end;
  parsed->query_start = query_start;
  parsed->has_query = query_start != size;
  parsed->path_end = query_start;
  return true;
}

}  // namespace

bool ValidateEntityId(const char *value, size_t size) {
  constexpr char kPrefix[] = "media_player.";
  constexpr size_t kObjectIdStart = sizeof(kPrefix) - 1;
  if (value == nullptr || size <= kObjectIdStart ||
      size > kMaxEntityIdBytes ||
      std::memcmp(value, kPrefix, kObjectIdStart) != 0)
    return false;

  bool previous_underscore = false;
  for (size_t i = kObjectIdStart; i < size; ++i) {
    const unsigned char c = value[i];
    if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_'))
      return false;
    if (c == '_') {
      if (previous_underscore || i == kObjectIdStart || i + 1 == size)
        return false;
      previous_underscore = true;
    } else {
      previous_underscore = false;
    }
  }
  return true;
}

bool NormalizeHomeAssistantUrl(const char *value, size_t size,
                               std::string *normalized) {
  if (normalized == nullptr || size == 0 || size > kMaxHomeAssistantUrlBytes)
    return false;
  ParsedUrl parsed;
  if (!ParseAbsoluteHttpUrl(value, size, false, &parsed)) return false;
  size_t output_size = parsed.path_end;
  while (output_size > parsed.authority_end && value[output_size - 1] == '/')
    --output_size;
  normalized->assign(value, output_size);
  return normalized->size() <= kMaxHomeAssistantUrlBytes;
}

bool MakeConfigRecord(const char *entity_id, size_t entity_id_size,
                      const char *home_assistant_url,
                      size_t home_assistant_url_size, uint32_t revision,
                      ConfigRecord *record) {
  if (record == nullptr || revision == 0 ||
      !ValidateEntityId(entity_id, entity_id_size))
    return false;
  std::string normalized;
  if (!NormalizeHomeAssistantUrl(home_assistant_url, home_assistant_url_size, &normalized))
    return false;
  ConfigRecord next{};
  next.format_version = kConfigFormatVersion;
  next.valid_marker = kConfigValidMarker;
  next.entity_id_size = static_cast<uint16_t>(entity_id_size);
  next.home_assistant_url_size = static_cast<uint16_t>(normalized.size());
  next.revision = revision;
  std::memcpy(next.entity_id, entity_id, entity_id_size);
  std::memcpy(next.home_assistant_url, normalized.data(), normalized.size());
  *record = next;
  return true;
}

bool ValidateConfigRecord(const ConfigRecord &record) {
  if (record.format_version != kConfigFormatVersion ||
      record.valid_marker != kConfigValidMarker ||
      record.entity_id_size > kMaxEntityIdBytes ||
      record.home_assistant_url_size > kMaxHomeAssistantUrlBytes ||
      record.revision == 0 ||
      record.entity_id[record.entity_id_size] != '\0' ||
      record.home_assistant_url[record.home_assistant_url_size] != '\0' ||
      !ValidateEntityId(record.entity_id, record.entity_id_size))
    return false;
  std::string normalized;
  return NormalizeHomeAssistantUrl(record.home_assistant_url,
                                   record.home_assistant_url_size, &normalized) &&
         normalized.size() == record.home_assistant_url_size &&
         std::memcmp(normalized.data(), record.home_assistant_url, normalized.size()) == 0;
}

bool ResolveArtworkUrl(const char *base_url, size_t base_url_size,
                       const char *entity_picture, size_t entity_picture_size,
                       std::string *resolved) {
  if (resolved == nullptr || entity_picture == nullptr || entity_picture_size == 0 ||
      entity_picture_size > kMaxResolvedArtworkUrlBytes ||
      HasUnsafeByte(entity_picture, entity_picture_size))
    return false;
  ParsedUrl parsed;
  if (ParseAbsoluteHttpUrl(entity_picture, entity_picture_size, true, &parsed)) {
    resolved->assign(entity_picture, entity_picture_size);
    return true;
  }
  if (entity_picture[0] != '/' || (entity_picture_size > 1 && entity_picture[1] == '/') ||
      std::memchr(entity_picture, '#', entity_picture_size) != nullptr)
    return false;
  std::string normalized_base;
  if (!NormalizeHomeAssistantUrl(base_url, base_url_size, &normalized_base) ||
      normalized_base.size() + entity_picture_size > kMaxResolvedArtworkUrlBytes)
    return false;
  resolved->assign(normalized_base);
  resolved->append(entity_picture, entity_picture_size);
  return true;
}

std::string RedactUrlForDiagnostics(const char *value, size_t size) {
  ParsedUrl parsed;
  if (!ParseAbsoluteHttpUrl(value, size, true, &parsed)) return "<redacted>";
  if (parsed.has_query)
    return std::string(value, parsed.query_start) + "?<redacted>";
  return std::string(value, size);
}

}  // namespace esphome::pixoo64::now_playing_config
