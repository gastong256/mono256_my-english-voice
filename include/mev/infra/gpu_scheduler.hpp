#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>

namespace mev {

// ---------------------------------------------------------------------------
// GpuScheduler — lightweight GPU access coordinator for ASR-priority scheduling.
//
// Policy: ASR has absolute priority over TTS for GPU access.
//   - ASR always acquires immediately (non-blocking).
//   - TTS acquires only when ASR is not active.
//   - If TTS is running when ASR needs the GPU, TTS finishes its current
//     inference (we don't interrupt mid-inference) but won't start a new one
//     until ASR releases.
//
// Implementation: a single atomic counter avoids any mutex in the hot path.
//   asr_active_count_ > 0  → ASR is running; TTS must wait.
//
// Thread-safety: all methods are safe to call from any thread.
// ---------------------------------------------------------------------------
class GpuScheduler {
 public:
  // ---- ASR side (always granted) ----------------------------------------

  // Mark ASR inference as started. Returns immediately.
  void asr_acquire() noexcept {
    asr_active_count_.fetch_add(1U, std::memory_order_acq_rel);
  }

  // Mark ASR inference as finished.
  void asr_release() noexcept {
    asr_active_count_.fetch_sub(1U, std::memory_order_acq_rel);
  }

  // ---- TTS side (conditional on no active ASR) --------------------------

  // Try to acquire GPU for TTS. Returns true if granted, false if ASR is active.
  // Non-blocking. Caller should retry or fall back to CPU.
  [[nodiscard]] bool tts_try_acquire() noexcept {
    if (asr_active_count_.load(std::memory_order_acquire) > 0U) {
      gpu_contention_events_.fetch_add(1U, std::memory_order_relaxed);
      return false;
    }
    tts_active_.store(true, std::memory_order_release);
    // Double-check: ASR may have acquired between our check and our store.
    if (asr_active_count_.load(std::memory_order_acquire) > 0U) {
      tts_active_.store(false, std::memory_order_release);
      gpu_contention_events_.fetch_add(1U, std::memory_order_relaxed);
      return false;
    }
    return true;
  }

  // Release the GPU from TTS use.
  void tts_release() noexcept {
    tts_active_.store(false, std::memory_order_release);
  }

  // ---- Observability ----------------------------------------------------

  [[nodiscard]] bool is_asr_active() const noexcept {
    return asr_active_count_.load(std::memory_order_acquire) > 0U;
  }

  [[nodiscard]] bool is_tts_active() const noexcept {
    return tts_active_.load(std::memory_order_acquire);
  }

  // Total number of times TTS could not acquire GPU due to active ASR.
  [[nodiscard]] std::uint64_t contention_events() const noexcept {
    return gpu_contention_events_.load(std::memory_order_relaxed);
  }

  // Reset contention counter (e.g., for periodic reporting).
  void reset_contention_count() noexcept {
    gpu_contention_events_.store(0U, std::memory_order_relaxed);
  }

 private:
  // Number of concurrent ASR inferences holding the GPU.
  // In our single-ASR-thread design this is 0 or 1, but the counter is safe
  // for future multi-ASR configurations.
  alignas(64) std::atomic<std::uint32_t> asr_active_count_{0};

  // True while a TTS inference holds the GPU.
  alignas(64) std::atomic<bool> tts_active_{false};

  // Diagnostic counter: incremented each time TTS is denied GPU access.
  alignas(64) std::atomic<std::uint64_t> gpu_contention_events_{0};
};

// ---------------------------------------------------------------------------
// RAII guard for ASR GPU acquisition.
// ---------------------------------------------------------------------------
class AsrGpuGuard {
 public:
  explicit AsrGpuGuard(GpuScheduler& scheduler) : scheduler_(scheduler) {
    scheduler_.asr_acquire();
  }
  ~AsrGpuGuard() { scheduler_.asr_release(); }

  AsrGpuGuard(const AsrGpuGuard&) = delete;
  AsrGpuGuard& operator=(const AsrGpuGuard&) = delete;

 private:
  GpuScheduler& scheduler_;
};

}  // namespace mev
