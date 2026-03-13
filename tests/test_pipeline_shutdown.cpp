// test_pipeline_shutdown.cpp
// Integration test: start pipeline, inject a few chunks, request clean shutdown.
// Verifies: all worker threads terminate, no deadlock, no memory leak (ASan).

#include <cassert>
#include <chrono>
#include <thread>

#include "mev/app/application.hpp"
#include "mev/config/app_config.hpp"
#include "mev/pipeline/pipeline_orchestrator.hpp"

using namespace mev;

static void test_start_and_stop_immediately() {
  AppConfig cfg = default_config();
  cfg.runtime.use_simulated_audio  = true;
  cfg.runtime.run_duration_seconds = 0;  // not used here; we control lifetime

  PipelineOrchestrator orch(cfg);

  const bool started = orch.start();
  assert(started && "pipeline must start successfully with default config");
  assert(orch.state() == PipelineState::kRunning);

  // Let it process a few cycles.
  std::this_thread::sleep_for(std::chrono::milliseconds(200));

  orch.stop();
  assert(orch.state() == PipelineState::kStopped && "pipeline must stop cleanly");
}

static void test_stop_idempotent() {
  AppConfig cfg = default_config();
  cfg.runtime.use_simulated_audio = true;

  PipelineOrchestrator orch(cfg);
  assert(orch.start());

  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  // Multiple stop() calls must not crash or deadlock.
  orch.stop();
  orch.stop();
  orch.stop();

  assert(orch.state() == PipelineState::kStopped);
}

static void test_double_start_rejected() {
  AppConfig cfg = default_config();
  cfg.runtime.use_simulated_audio = true;

  PipelineOrchestrator orch(cfg);
  assert(orch.start());

  // Second start() while running must return false.
  const bool second_start = orch.start();
  assert(!second_start && "double start must be rejected");

  orch.stop();
}

static void test_metrics_non_zero_after_run() {
  AppConfig cfg = default_config();
  cfg.runtime.use_simulated_audio = true;
  cfg.telemetry.log_per_utterance = false;
  // Use a short chunk so 500ms produces several chunks:
  // chunk_samples = 160ms * 48000 / 1000 = 7680; 500ms / 10ms * 480 = 24000 >> 7680.
  cfg.asr.chunk_ms = 160;
  cfg.asr.hop_ms   = 80;

  PipelineOrchestrator orch(cfg);
  assert(orch.start());

  // Run long enough for ingest to produce some ASR requests.
  std::this_thread::sleep_for(std::chrono::milliseconds(500));

  const auto snap = orch.metrics_snapshot();
  assert(snap.asr_requests > 0 && "at least one ASR request must be processed");

  orch.stop();
}

static void test_destructor_cleans_up() {
  AppConfig cfg = default_config();
  cfg.runtime.use_simulated_audio = true;

  {
    PipelineOrchestrator orch(cfg);
    assert(orch.start());
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    // Destructor calls stop() — must not crash or leak.
  }
  // If we reach here, destructor was clean.
}

int main() {
  test_start_and_stop_immediately();
  test_stop_idempotent();
  test_double_start_rejected();
  test_metrics_non_zero_after_run();
  test_destructor_cleans_up();
  return 0;
}
