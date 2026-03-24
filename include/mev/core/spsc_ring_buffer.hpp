#pragma once

#include <atomic>
#include <cstddef>
#include <memory>
#include <new>
#include <type_traits>
#include <utility>

namespace mev {

// C4324: structure padded due to alignment specifier — intentional; head_ and
// tail_ live on separate cache lines to eliminate false sharing between the
// producer and consumer threads.
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4324)
#endif
template <typename T>
class SpscRingBuffer {
 public:
  explicit SpscRingBuffer(std::size_t capacity)
      : capacity_(capacity + 1U), storage_(std::make_unique<Storage[]>(capacity_)) {}

  SpscRingBuffer(const SpscRingBuffer&) = delete;
  SpscRingBuffer& operator=(const SpscRingBuffer&) = delete;

  ~SpscRingBuffer() { clear(); }

  [[nodiscard]] std::size_t capacity() const { return capacity_ - 1U; }

  [[nodiscard]] std::size_t size_approx() const {
    const auto head = head_.load(std::memory_order_acquire);
    const auto tail = tail_.load(std::memory_order_acquire);
    return head >= tail ? (head - tail) : (capacity_ - (tail - head));
  }

  [[nodiscard]] bool try_push(const T& value) {
    const auto head = head_.load(std::memory_order_relaxed);
    const auto next = increment(head);
    if (next == tail_.load(std::memory_order_acquire)) {
      return false;
    }

    new (&storage_[head]) T(value);
    head_.store(next, std::memory_order_release);
    return true;
  }

  [[nodiscard]] bool try_push(T&& value) {
    const auto head = head_.load(std::memory_order_relaxed);
    const auto next = increment(head);
    if (next == tail_.load(std::memory_order_acquire)) {
      return false;
    }

    new (&storage_[head]) T(std::move(value));
    head_.store(next, std::memory_order_release);
    return true;
  }

  template <typename... Args>
  [[nodiscard]] bool try_emplace(Args&&... args) {
    const auto head = head_.load(std::memory_order_relaxed);
    const auto next = increment(head);
    if (next == tail_.load(std::memory_order_acquire)) {
      return false;
    }

    new (&storage_[head]) T(std::forward<Args>(args)...);
    head_.store(next, std::memory_order_release);
    return true;
  }

  [[nodiscard]] bool try_pop(T& out) {
    const auto tail = tail_.load(std::memory_order_relaxed);
    if (tail == head_.load(std::memory_order_acquire)) {
      return false;
    }

    T* ptr = std::launder(reinterpret_cast<T*>(&storage_[tail]));
    out = std::move(*ptr);
    ptr->~T();
    tail_.store(increment(tail), std::memory_order_release);
    return true;
  }

  void clear() {
    auto tail = tail_.load(std::memory_order_relaxed);
    const auto head = head_.load(std::memory_order_relaxed);
    while (tail != head) {
      T* ptr = std::launder(reinterpret_cast<T*>(&storage_[tail]));
      ptr->~T();
      tail = increment(tail);
    }
    tail_.store(tail, std::memory_order_relaxed);
  }

 private:
  using Storage = std::aligned_storage_t<sizeof(T), alignof(T)>;

  [[nodiscard]] std::size_t increment(const std::size_t index) const {
    return (index + 1U) % capacity_;
  }

  const std::size_t capacity_;
  std::unique_ptr<Storage[]> storage_;

  alignas(64) std::atomic<std::size_t> head_{0};
  alignas(64) std::atomic<std::size_t> tail_{0};
};
#ifdef _MSC_VER
#pragma warning(pop)
#endif

}  // namespace mev
