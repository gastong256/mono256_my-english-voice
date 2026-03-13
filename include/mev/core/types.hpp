#pragma once

#include <chrono>
#include <cstdint>

namespace mev {

using Clock = std::chrono::steady_clock;
using TimePoint = Clock::time_point;
using DurationMicros = std::chrono::microseconds;
using SequenceNumber = std::uint64_t;

inline std::int64_t to_micros(const Clock::duration duration) {
  return std::chrono::duration_cast<DurationMicros>(duration).count();
}

inline std::int64_t now_micros() {
  return to_micros(Clock::now().time_since_epoch());
}

}  // namespace mev
