#include <cassert>
#include <memory>
#include <thread>

#include "render_worker_queue.h"

using esphome::pixoo64::RenderWorkerQueue;

int main() {
  using Queue = RenderWorkerQueue<std::unique_ptr<int>, 2>;
  Queue::Storage storage;
  Queue queue(&storage);
  auto first = std::make_unique<int>(1);
  auto second = std::make_unique<int>(2);
  auto third = std::make_unique<int>(3);
  assert(queue.TryPush(std::move(first)));
  assert(queue.TryPush(std::move(second)));
  assert(!queue.TryPush(std::move(third)) && third && *third == 3);
  assert(**queue.Peek() == 1);
  std::unique_ptr<int> value;
  assert(queue.TryPop(value) && *value == 1);
  assert(queue.TryPush(std::move(third)));
  assert(queue.TryPop(value) && *value == 2);
  assert(queue.TryPop(value) && *value == 3);
  assert(!queue.TryPop(value));

  using IntQueue = RenderWorkerQueue<int, 32>;
  IntQueue::Storage ints;
  IntQueue concurrent(&ints);
  std::thread producer([&] {
    for (int i = 0; i < 100000; ++i) {
      int next = i;
      while (!concurrent.TryPush(std::move(next))) std::this_thread::yield();
    }
  });
  for (int i = 0; i < 100000; ++i) {
    int next;
    while (!concurrent.TryPop(next)) std::this_thread::yield();
    assert(next == i);
  }
  producer.join();
}
