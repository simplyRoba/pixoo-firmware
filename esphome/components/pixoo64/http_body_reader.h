#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>

namespace esphome::http_request {
class HttpRequestComponent;
}

namespace esphome::pixoo64::adapters {

class HttpRequestGate;

namespace http_body {

enum class ReadStatus : uint8_t {
  kSuccess,
  kCancelled,
  kUnavailable,
  kInvalidOptions,
  kRequestFailed,
  kHttpStatus,
  kAdvertisedTooLarge,
  kAllocationFailed,
  kDeadlineExceeded,
  kNoProgressTimeout,
  kReadError,
  kOverflow,
  kIncomplete,
};

const char *ReadStatusName(ReadStatus status);

// Owns a capability-qualified heap allocation until moved or released.
class HeapBuffer {
public:
  HeapBuffer() = default;
  ~HeapBuffer();
  HeapBuffer(const HeapBuffer &) = delete;
  HeapBuffer &operator=(const HeapBuffer &) = delete;
  HeapBuffer(HeapBuffer &&other) noexcept;
  HeapBuffer &operator=(HeapBuffer &&other) noexcept;

  static HeapBuffer Allocate(size_t size, uint32_t capabilities);
  uint8_t *data() const { return this->data_; }
  size_t capacity() const { return this->capacity_; }
  explicit operator bool() const { return this->data_ != nullptr; }
  uint8_t *Release();
  void Reset();

private:
  HeapBuffer(uint8_t *data, size_t capacity)
      : data_(data), capacity_(capacity) {}

  uint8_t *data_{nullptr};
  size_t capacity_{0};
};

struct ReadOptions {
  size_t max_bytes{0};
  size_t chunk_bytes{0};
  uint32_t allocation_capabilities{0};
};

// The result deliberately carries no request URL or headers. Callers can log
// its status and received_bytes without exposing signed artwork URLs or tokens.
struct ReadResult {
  ReadStatus status{ReadStatus::kInvalidOptions};
  size_t received_bytes{0};
  int http_status{-1};
  HeapBuffer body{};

  bool succeeded() const { return this->status == ReadStatus::kSuccess; }
};

// Holds HttpRequestGate from get() through container->end(). The returned body
// is available only after the container has ended and the gate is released.
ReadResult GetBounded(http_request::HttpRequestComponent *http,
                      HttpRequestGate *gate, const std::string &url,
                      const ReadOptions &options,
                      const std::function<bool()> &cancelled);

} // namespace http_body
} // namespace esphome::pixoo64::adapters
