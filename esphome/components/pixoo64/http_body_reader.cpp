#include "http_body_reader.h"

#include <algorithm>
#include <utility>

#include "esp_heap_caps.h"

#include "esphome/components/http_request/http_request.h"
#include "esphome/core/hal.h"
#include "http_body_policy.h"
#include "http_request_gate.h"

namespace esphome::pixoo64::adapters::http_body {
namespace {

bool DeadlinePassed(uint32_t started_ms, uint32_t timeout_ms) {
  return millis() - started_ms >= timeout_ms;
}

} // namespace

const char *ReadStatusName(ReadStatus status) {
  switch (status) {
  case ReadStatus::kSuccess:
    return "success";
  case ReadStatus::kCancelled:
    return "cancelled";
  case ReadStatus::kUnavailable:
    return "unavailable";
  case ReadStatus::kInvalidOptions:
    return "invalid options";
  case ReadStatus::kRequestFailed:
    return "request failed";
  case ReadStatus::kHttpStatus:
    return "HTTP status";
  case ReadStatus::kAdvertisedTooLarge:
    return "advertised body too large";
  case ReadStatus::kAllocationFailed:
    return "allocation failed";
  case ReadStatus::kDeadlineExceeded:
    return "body deadline exceeded";
  case ReadStatus::kNoProgressTimeout:
    return "body read timed out";
  case ReadStatus::kReadError:
    return "body read error";
  case ReadStatus::kOverflow:
    return "body exceeds limit";
  case ReadStatus::kIncomplete:
    return "body incomplete";
  }
  return "unknown";
}

HeapBuffer::~HeapBuffer() { this->Reset(); }

HeapBuffer::HeapBuffer(HeapBuffer &&other) noexcept
    : data_(other.data_), capacity_(other.capacity_) {
  other.data_ = nullptr;
  other.capacity_ = 0;
}

HeapBuffer &HeapBuffer::operator=(HeapBuffer &&other) noexcept {
  if (this != &other) {
    this->Reset();
    this->data_ = other.data_;
    this->capacity_ = other.capacity_;
    other.data_ = nullptr;
    other.capacity_ = 0;
  }
  return *this;
}

HeapBuffer HeapBuffer::Allocate(size_t size, uint32_t capabilities) {
  if (size == 0)
    return {};
  return HeapBuffer(
      static_cast<uint8_t *>(heap_caps_malloc(size, capabilities)), size);
}

uint8_t *HeapBuffer::Release() {
  uint8_t *data = this->data_;
  this->data_ = nullptr;
  this->capacity_ = 0;
  return data;
}

void HeapBuffer::Reset() {
  if (this->data_ != nullptr)
    heap_caps_free(this->data_);
  this->data_ = nullptr;
  this->capacity_ = 0;
}

ReadResult GetBounded(http_request::HttpRequestComponent *http,
                      HttpRequestGate *gate, const std::string &url,
                      const ReadOptions &options,
                      const std::function<bool()> &cancelled) {
  ReadResult result;
  if (http == nullptr || gate == nullptr || !cancelled ||
      options.max_bytes == 0 || options.chunk_bytes == 0 ||
      options.allocation_capabilities == 0)
    return result;
  if (cancelled()) {
    result.status = ReadStatus::kCancelled;
    return result;
  }

  auto lease = gate->Acquire(cancelled);
  if (!lease) {
    result.status =
        cancelled() ? ReadStatus::kCancelled : ReadStatus::kUnavailable;
    return result;
  }
  // Acquire() checks this too, but preserve the boundary if its implementation
  // changes or cancellation races the successful semaphore take.
  if (cancelled()) {
    lease.Release();
    result.status = ReadStatus::kCancelled;
    return result;
  }

  auto container = http->get(url);
  if (container == nullptr) {
    lease.Release();
    result.status = ReadStatus::kRequestFailed;
    return result;
  }

  result.http_status = container->status_code;
  if (cancelled()) {
    result.status = ReadStatus::kCancelled;
  } else if (container->status_code != 200) {
    result.status = ReadStatus::kHttpStatus;
  } else {
    // ESPHome reports zero for chunked or empty bodies. Both are safely
    // handled as unknown-length transfers; every nonzero Content-Length is
    // available directly on the response container.
    const size_t advertised_bytes = container->content_length;
    const bool has_content_length = advertised_bytes != 0;
    if (!::esphome::pixoo64::http_body::AcceptAdvertisedSize(
            advertised_bytes, has_content_length, options.max_bytes)) {
      result.status = ReadStatus::kAdvertisedTooLarge;
    } else {
      const size_t capacity =
          has_content_length ? advertised_bytes : options.max_bytes;
      HeapBuffer candidate =
          HeapBuffer::Allocate(capacity, options.allocation_capabilities);
      if (capacity != 0 && !candidate) {
        result.status = ReadStatus::kAllocationFailed;
      } else {
        const uint32_t started_ms = millis();
        const uint32_t timeout_ms = http->get_timeout();
        uint32_t last_data_ms = started_ms;
        bool transfer_complete = false;
        while (true) {
          if (cancelled()) {
            result.status = ReadStatus::kCancelled;
            break;
          }
          if (DeadlinePassed(started_ms, timeout_ms)) {
            result.status = ReadStatus::kDeadlineExceeded;
            break;
          }
          if (result.received_bytes > capacity) {
            result.status = ReadStatus::kOverflow;
            break;
          }

          const size_t remaining = capacity - result.received_bytes;
          const bool at_limit = remaining == 0;
          uint8_t overflow_byte{};
          const size_t read_size =
              at_limit ? 1 : std::min(remaining, options.chunk_bytes);
          uint8_t *destination = at_limit
                                     ? &overflow_byte
                                     : candidate.data() + result.received_bytes;
          const int read = container->read(destination, read_size);

          // ESP-IDF reads may block until their transport timeout. The second
          // deadline check rejects data returned after the whole-body deadline;
          // cancellation remains cooperative between read calls.
          if (cancelled()) {
            result.status = ReadStatus::kCancelled;
            break;
          }
          if (DeadlinePassed(started_ms, timeout_ms)) {
            result.status = ReadStatus::kDeadlineExceeded;
            break;
          }

          const auto step = http_request::http_read_loop_result(
              read, last_data_ms, timeout_ms, container->is_read_complete());
          if (step == http_request::HttpReadLoopResult::DATA) {
            if (at_limit || static_cast<size_t>(read) > remaining) {
              result.status = ReadStatus::kOverflow;
              break;
            }
            result.received_bytes += static_cast<size_t>(read);
            continue;
          }
          if (step == http_request::HttpReadLoopResult::COMPLETE) {
            transfer_complete = true;
            break;
          }
          if (step == http_request::HttpReadLoopResult::RETRY)
            continue;
          result.status = step == http_request::HttpReadLoopResult::TIMEOUT
                              ? ReadStatus::kNoProgressTimeout
                              : ReadStatus::kReadError;
          break;
        }

        if (result.status == ReadStatus::kInvalidOptions) {
          if (::esphome::pixoo64::http_body::IsCompleteBody(
                  result.received_bytes, advertised_bytes, has_content_length,
                  options.max_bytes, transfer_complete)) {
            result.status = ReadStatus::kSuccess;
            result.body = std::move(candidate);
          } else {
            result.status = ReadStatus::kIncomplete;
          }
        }
      }
    }
  }

  // The singleton transport remains protected until end() returns. Parsing and
  // decoding happen only after this explicit release.
  container->end();
  lease.Release();
  if (result.succeeded() && cancelled()) {
    result.body.Reset();
    result.status = ReadStatus::kCancelled;
  }
  return result;
}

} // namespace esphome::pixoo64::adapters::http_body
