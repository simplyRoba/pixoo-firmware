"""Exercise the source snapshot allocator and fail-closed behavior on the host."""
import pathlib
import subprocess
import tempfile
import unittest


class SnapshotBufferTest(unittest.TestCase):
    def test_allocation_failure_and_lifetime(self):
        adapter = pathlib.Path(__file__).resolve().parents[2] / 'esphome/components/pixoo64'
        with tempfile.TemporaryDirectory() as tmp:
            root = pathlib.Path(tmp)
            (root / 'freertos').mkdir()
            (root / 'freertos/FreeRTOS.h').write_text('''
#pragma once
using portMUX_TYPE = int;
#define portMUX_INITIALIZER_UNLOCKED 0
#define taskENTER_CRITICAL(mux) ((void)(mux))
#define taskEXIT_CRITICAL(mux) ((void)(mux))
''')
            (root / 'freertos/task.h').write_text('#pragma once\n')
            (root / 'esp_heap_caps.h').write_text('''
#pragma once
#include <cstddef>
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
void *heap_caps_malloc(std::size_t, int);
void heap_caps_free(void *);
''')
            (root / 'test.cpp').write_text('''
#include <cassert>
#include <cstdlib>
#include "snapshot_buffer.h"
using esphome::pixoo64::adapters::async::SnapshotBuffer;
static bool fail = true;
static int allocations = 0, frees = 0;
void *heap_caps_malloc(std::size_t n, int caps) {
  assert(caps == (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  ++allocations;
  return fail ? nullptr : std::malloc(n);
}
void heap_caps_free(void *p) { ++frees; std::free(p); }
struct Value { int n = 7; };
int main() {
  {
    SnapshotBuffer<Value> b;
    b.Publish({42});
    Value out{99};
    assert(!b.Read(&out) && out.n == 99 && !b.Read(nullptr));
    assert(!b.has_value() && b.Get().n == 7);
    b.Clear();
    assert(!b.Initialize());
    b.Publish({42});
    assert(!b.has_value() && b.Get().n == 7);
    fail = false;
    assert(b.Initialize() && b.Initialize());
    assert(allocations == 2 && !b.has_value());
    b.Publish({42});
    assert(b.has_value() && b.Get().n == 42);
    assert(b.Read(&out) && out.n == 42);
    b.Clear();
    assert(!b.Read(&out) && out.n == 42);
    assert(!b.has_value() && b.Get().n == 7);
  }
  assert(frees == 1);
}
''')
            subprocess.run(['c++', '-std=c++17', '-I', str(root), '-I', str(adapter),
                            str(root / 'test.cpp'), '-o', str(root / 'test')], check=True)
            subprocess.run([str(root / 'test')], check=True)


if __name__ == '__main__':
    unittest.main()
