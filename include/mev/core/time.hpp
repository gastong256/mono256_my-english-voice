#pragma once

#include <functional>

#include "mev/core/types.hpp"

namespace mev {

class ScopeTimer {
 public:
  using Callback = std::function<void(std::int64_t)>;

  explicit ScopeTimer(Callback callback) : callback_(std::move(callback)), start_(Clock::now()) {}

  ~ScopeTimer() {
    if (callback_) {
      callback_(to_micros(Clock::now() - start_));
    }
  }

 private:
  Callback callback_;
  TimePoint start_;
};

}  // namespace mev
