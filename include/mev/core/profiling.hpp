#pragma once

// =============================================================================
// Pipeline profiling macros.
//
// Enabled at compile time with -DMEV_ENABLE_PROFILING=ON (CMake option).
// When disabled, all macros expand to zero-cost no-ops.
//
// Usage in worker code:
//   MEV_PROFILE_BEGIN("asr_inference");
//   auto result = asr_engine_->transcribe(...);
//   MEV_PROFILE_END("asr_inference");
//
// Statistics (min / max / avg / count) are accumulated in a thread-local
// registry and reported by the Supervisor's monitor_loop().
// =============================================================================

#include <atomic>
#include <chrono>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <mutex>

namespace mev {

struct ProfileStats {
  std::uint64_t count{0};
  std::uint64_t total_us{0};
  std::uint64_t min_us{UINT64_MAX};
  std::uint64_t max_us{0};

  [[nodiscard]] double avg_us() const {
    return count > 0 ? static_cast<double>(total_us) / static_cast<double>(count) : 0.0;
  }
};

// Global profiling registry — thread-safe via a mutex (telemetry path only).
class ProfilingRegistry {
 public:
  static ProfilingRegistry& instance() {
    static ProfilingRegistry reg;
    return reg;
  }

  void record(const char* tag, std::uint64_t micros) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto& s = stats_[tag];
    ++s.count;
    s.total_us += micros;
    if (micros < s.min_us) s.min_us = micros;
    if (micros > s.max_us) s.max_us = micros;
  }

  // Snapshot current stats (does NOT reset them).
  [[nodiscard]] std::unordered_map<std::string, ProfileStats> snapshot() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return stats_;
  }

  void reset() {
    std::lock_guard<std::mutex> lock(mutex_);
    stats_.clear();
  }

 private:
  mutable std::mutex mutex_;
  std::unordered_map<std::string, ProfileStats> stats_;
};

// RAII scope timer used by the macros below.
class ProfileScope {
 public:
  ProfileScope(const char* tag) : tag_(tag), start_(std::chrono::steady_clock::now()) {}
  ~ProfileScope() {
    const auto end = std::chrono::steady_clock::now();
    const auto us  = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(end - start_).count());
    ProfilingRegistry::instance().record(tag_, us);
  }
  ProfileScope(const ProfileScope&) = delete;
  ProfileScope& operator=(const ProfileScope&) = delete;

 private:
  const char* tag_;
  std::chrono::steady_clock::time_point start_;
};

}  // namespace mev

// ---------------------------------------------------------------------------
// Macro API — zero cost when profiling is disabled.
// ---------------------------------------------------------------------------
#if defined(MEV_ENABLE_PROFILING)
  #define MEV_PROFILE_SCOPE(tag)   ::mev::ProfileScope _prof_##__LINE__((tag))
  #define MEV_PROFILE_BEGIN(tag)   const auto _prof_begin_##__LINE__ = std::chrono::steady_clock::now(); \
                                   const char* _prof_tag_##__LINE__ = (tag)
  #define MEV_PROFILE_END(tag)     do { \
    const auto _us = static_cast<std::uint64_t>( \
        std::chrono::duration_cast<std::chrono::microseconds>( \
            std::chrono::steady_clock::now() - _prof_begin_##__LINE__).count()); \
    ::mev::ProfilingRegistry::instance().record(_prof_tag_##__LINE__, _us); \
  } while (0)
#else
  #define MEV_PROFILE_SCOPE(tag)   do {} while (0)
  #define MEV_PROFILE_BEGIN(tag)   do {} while (0)
  #define MEV_PROFILE_END(tag)     do {} while (0)
#endif
