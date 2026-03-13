#include <cassert>
#include <string>

#include "mev/core/spsc_ring_buffer.hpp"

int main() {
  mev::SpscRingBuffer<std::string> queue(4);

  assert(queue.capacity() == 4);
  assert(queue.try_push("one"));
  assert(queue.try_push("two"));
  assert(queue.size_approx() == 2);

  std::string value;
  assert(queue.try_pop(value));
  assert(value == "one");
  assert(queue.try_pop(value));
  assert(value == "two");
  assert(!queue.try_pop(value));

  assert(queue.try_push("a"));
  assert(queue.try_push("b"));
  assert(queue.try_push("c"));
  assert(queue.try_push("d"));
  assert(!queue.try_push("overflow"));

  return 0;
}
