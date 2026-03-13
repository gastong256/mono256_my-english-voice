#include <chrono>
#include <cstdint>
#include <iostream>
#include <thread>

#include "mev/core/spsc_ring_buffer.hpp"

int main() {
  mev::SpscRingBuffer<std::uint64_t> queue(1 << 14);
  constexpr std::uint64_t iterations = 2'000'000;

  auto start = std::chrono::steady_clock::now();

  std::jthread producer([&](std::stop_token) {
    for (std::uint64_t i = 0; i < iterations; ++i) {
      while (!queue.try_push(i)) {
      }
    }
  });

  std::jthread consumer([&](std::stop_token) {
    std::uint64_t value = 0;
    for (std::uint64_t i = 0; i < iterations; ++i) {
      while (!queue.try_pop(value)) {
      }
    }
  });

  producer.join();
  consumer.join();

  auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start);
  std::cout << "queue throughput test: " << iterations << " ops in " << elapsed.count() << "ms\n";
  return 0;
}
