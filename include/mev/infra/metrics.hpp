#pragma once

#include <array>
#include <atomic>
#include <cstdint>

#include "mev/core/types.hpp"

namespace mev {

enum class StageId : std::size_t {
  kAudioIngest = 0,
  kAsr,
  kCommit,       // kept for backward compat; maps to text processing in new pipeline
  kNormalize,
  kTts,
  kEndToEnd,
  kCount
};

struct StageSnapshot {
  std::uint64_t count{0};
  std::uint64_t total_us{0};
  std::uint64_t max_us{0};
  std::uint64_t last_us{0};
};

struct MetricsSnapshot {
  std::uint64_t input_overruns{0};
  std::uint64_t output_underruns{0};
  std::uint64_t queue_drops{0};
  std::uint64_t stale_cancelled{0};
  std::uint64_t asr_requests{0};
  std::uint64_t tts_requests{0};
  std::uint64_t gpu_contention_fallbacks{0};  // times TTS fell back to CPU due to ASR
  std::uint64_t degradation_events{0};         // mode transitions or fallback activations
  std::array<StageSnapshot, static_cast<std::size_t>(StageId::kCount)> stages{};
};

class MetricsRegistry {
 public:
  void inc_input_overrun()   { input_overruns_.fetch_add(1, std::memory_order_relaxed); }
  void inc_output_underrun() { output_underruns_.fetch_add(1, std::memory_order_relaxed); }
  void inc_queue_drop()      { queue_drops_.fetch_add(1, std::memory_order_relaxed); }
  void inc_stale_cancelled() { stale_cancelled_.fetch_add(1, std::memory_order_relaxed); }
  void inc_asr_requests()    { asr_requests_.fetch_add(1, std::memory_order_relaxed); }
  void inc_tts_requests()    { tts_requests_.fetch_add(1, std::memory_order_relaxed); }
  void inc_gpu_contention_fallback() { gpu_contention_fallbacks_.fetch_add(1, std::memory_order_relaxed); }
  void inc_degradation_event()       { degradation_events_.fetch_add(1, std::memory_order_relaxed); }

  void record_stage_latency(StageId stage, std::uint64_t micros);

  [[nodiscard]] MetricsSnapshot snapshot() const;

 private:
  struct StageCounters {
    std::atomic<std::uint64_t> count{0};
    std::atomic<std::uint64_t> total_us{0};
    std::atomic<std::uint64_t> max_us{0};
    std::atomic<std::uint64_t> last_us{0};
  };

  std::atomic<std::uint64_t> input_overruns_{0};
  std::atomic<std::uint64_t> output_underruns_{0};
  std::atomic<std::uint64_t> queue_drops_{0};
  std::atomic<std::uint64_t> stale_cancelled_{0};
  std::atomic<std::uint64_t> asr_requests_{0};
  std::atomic<std::uint64_t> tts_requests_{0};
  std::atomic<std::uint64_t> gpu_contention_fallbacks_{0};
  std::atomic<std::uint64_t> degradation_events_{0};

  std::array<StageCounters, static_cast<std::size_t>(StageId::kCount)> stages_{};
};

// ---------------------------------------------------------------------------
// StageTimer — RAII scope timer for pipeline stage latency tracking.
// ---------------------------------------------------------------------------
class StageTimer {
 public:
  StageTimer(MetricsRegistry& metrics, StageId stage)
      : metrics_(metrics), stage_(stage), start_(Clock::now()) {}

  ~StageTimer() {
    metrics_.record_stage_latency(
        stage_, static_cast<std::uint64_t>(to_micros(Clock::now() - start_)));
  }

  StageTimer(const StageTimer&) = delete;
  StageTimer& operator=(const StageTimer&) = delete;

 private:
  MetricsRegistry& metrics_;
  StageId stage_;
  TimePoint start_;
};

}  // namespace mev
