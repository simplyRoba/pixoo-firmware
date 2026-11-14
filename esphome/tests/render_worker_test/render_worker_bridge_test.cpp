#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <new>
#include <utility>

#include "render_worker_bridge.h"

using namespace esphome::pixoo64;

int main() {
  assert(WorkerSequenceBefore(1, 2));
  assert(!WorkerSequenceBefore(2, 2));
  assert(WorkerSequenceBefore(UINT32_MAX, 0));
  assert(!WorkerSequenceBefore(0, UINT32_MAX));
  WorkerCommandQueue::Storage storage;
  WorkerCommandQueue queue(&storage);
  WorkerCommand command;
  command.kind = WorkerCommandKind::kNotification;
  void *memory = std::malloc(sizeof(pixoo::NotificationRequest));
  assert(memory);
  command.notification.reset(new (memory) pixoo::NotificationRequest{});
  assert(queue.TryPush(std::move(command)));
  assert(!command.notification);
  WorkerCommand result;
  assert(queue.TryPop(result));
  assert(result.kind == WorkerCommandKind::kNotification && result.notification);
}
