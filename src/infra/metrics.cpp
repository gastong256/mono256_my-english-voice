#include "mev/infra/metrics.hpp"

namespace mev {

// inc_* methods are now inline in the header for zero-overhead on data path.

void MetricsRegistry::record_stage_latency(const StageId stage, const std::uint64_t micros) {
  auto& c = stages_[static_cast<std::size_t>(stage)];
  c.count.fetch_add(1, std::memory_order_relaxed);
  c.total_us.fetch_add(micros, std::memory_order_relaxed);
  c.last_us.store(micros, std::memory_order_relaxed);

  // CAS loop to track running maximum without a mutex.
  auto current_max = c.max_us.load(std::memory_order_relaxed);
  while (current_max < micros &&
         !c.max_us.compare_exchange_weak(current_max, micros, std::memory_order_relaxed)) {
  }
}

MetricsSnapshot MetricsRegistry::snapshot() const {
  MetricsSnapshot out;
  out.input_overruns          = input_overruns_.load(std::memory_order_relaxed);
  out.output_underruns        = output_underruns_.load(std::memory_order_relaxed);
  out.queue_drops             = queue_drops_.load(std::memory_order_relaxed);
  out.stale_cancelled         = stale_cancelled_.load(std::memory_order_relaxed);
  out.asr_requests            = asr_requests_.load(std::memory_order_relaxed);
  out.tts_requests            = tts_requests_.load(std::memory_order_relaxed);
  out.gpu_contention_fallbacks = gpu_contention_fallbacks_.load(std::memory_order_relaxed);
  out.degradation_events      = degradation_events_.load(std::memory_order_relaxed);

  for (std::size_t i = 0; i < stages_.size(); ++i) {
    out.stages[i] = StageSnapshot{
        .count    = stages_[i].count.load(std::memory_order_relaxed),
        .total_us = stages_[i].total_us.load(std::memory_order_relaxed),
        .max_us   = stages_[i].max_us.load(std::memory_order_relaxed),
        .last_us  = stages_[i].last_us.load(std::memory_order_relaxed),
    };
  }

  return out;
}

}  // namespace mev
