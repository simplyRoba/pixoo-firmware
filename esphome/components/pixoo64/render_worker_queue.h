#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <optional>
#include <utility>

namespace esphome::pixoo64 {

// Single producer and single consumer. Both must stop before storage is destroyed.
template<typename T, size_t Capacity> class RenderWorkerQueue {
  static_assert(Capacity > 0, "queue capacity must be positive");

 public:
  using Storage = std::array<std::optional<T>, Capacity + 1>;
  explicit RenderWorkerQueue(Storage *storage) : slots_(storage) {}
  RenderWorkerQueue(const RenderWorkerQueue &) = delete;
  RenderWorkerQueue &operator=(const RenderWorkerQueue &) = delete;

  // A failed push does not consume the argument.
  bool TryPush(T &&value) {
    if (!slots_) return false;
    const size_t tail = tail_.load(std::memory_order_relaxed);
    const size_t next = (tail + 1) % (Capacity + 1);
    if (next == head_.load(std::memory_order_acquire)) return false;
    (*slots_)[tail].emplace(std::move(value));
    tail_.store(next, std::memory_order_release);
    return true;
  }

  // Consumer only; valid until the next pop.
  const T *Peek() const {
    if (!slots_) return nullptr;
    const size_t head = head_.load(std::memory_order_relaxed);
    if (head == tail_.load(std::memory_order_acquire)) return nullptr;
    return &*(*slots_)[head];
  }

  bool TryPop(T &value) {
    if (!slots_) return false;
    const size_t head = head_.load(std::memory_order_relaxed);
    if (head == tail_.load(std::memory_order_acquire)) return false;
    value = std::move(*(*slots_)[head]);
    (*slots_)[head].reset();
    head_.store((head + 1) % (Capacity + 1), std::memory_order_release);
    return true;
  }

 private:
  Storage *slots_;
  std::atomic<size_t> head_{0};
  std::atomic<size_t> tail_{0};
};

}  // namespace esphome::pixoo64
