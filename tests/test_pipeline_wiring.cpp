#include <cassert>
#include <chrono>
#include <thread>

#include "mev/config/app_config.hpp"
#include "mev/pipeline/pipeline_orchestrator.hpp"

int main() {
  auto config = mev::default_config();
  config.runtime.use_simulated_audio = true;
  config.runtime.run_duration_seconds = 2;
  config.audio.frames_per_buffer = 240;
  config.asr.chunk_ms = 320;
  config.asr.hop_ms = 160;

  mev::PipelineOrchestrator orchestrator(config);
  const bool started = orchestrator.start();
  assert(started);

  std::this_thread::sleep_for(std::chrono::seconds(2));
  orchestrator.stop();

  const auto snapshot = orchestrator.metrics_snapshot();
  assert(snapshot.asr_requests > 0);

  return 0;
}
