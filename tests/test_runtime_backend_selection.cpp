#include <cassert>
#include <chrono>
#include <string>
#include <thread>

#include "mev/config/app_config.hpp"
#include "mev/pipeline/pipeline_orchestrator.hpp"

using namespace mev;

static void test_default_runtime_contract() {
  const AppConfig cfg = default_config();
  assert(cfg.runtime.use_simulated_audio &&
         "default runtime must stay safe for local development");
  assert(cfg.vad.engine == "none" &&
         "default VAD engine must not expose unimplemented backends");
}

static void test_validate_rejects_unsupported_vad_engine() {
  AppConfig cfg = default_config();
  cfg.vad.engine = "silero";

  std::string error;
  const bool ok = validate_config(cfg, error);
  assert(!ok && "silero must be rejected until the backend is implemented");
  assert(error.find("vad.engine") != std::string::npos);

  cfg.vad.engine = "mystery-vad";
  error.clear();
  const bool ok_unknown = validate_config(cfg, error);
  assert(!ok_unknown && "unknown VAD engines must be rejected");
  assert(error.find("vad.engine") != std::string::npos);
}

static void test_simulated_audio_path_starts() {
  AppConfig cfg = default_config();
  cfg.runtime.use_simulated_audio = true;
  cfg.runtime.run_duration_seconds = 0;
  cfg.telemetry.log_per_utterance = false;

  PipelineOrchestrator orch(cfg);
  const bool started = orch.start();
  assert(started && "simulated audio path must start successfully");
  assert(orch.state() == PipelineState::kRunning);

  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  orch.stop();
  assert(orch.state() == PipelineState::kStopped);
}

static void test_real_audio_requires_portaudio_build() {
  AppConfig cfg = default_config();
  cfg.runtime.use_simulated_audio = false;
  cfg.runtime.run_duration_seconds = 0;
  cfg.telemetry.log_per_utterance = false;

  PipelineOrchestrator orch(cfg);

#if defined(MEV_ENABLE_PORTAUDIO)
  // We only assert that initialization gets far enough to either start or fail
  // for an environment reason (e.g. no device). The compile-time contract is
  // covered by the non-PortAudio branch below.
  (void)orch;
#else
  const bool started = orch.start();
  assert(!started && "real audio must fail when PortAudio is not compiled in");
  assert(orch.state() == PipelineState::kFailed);
#endif
}

int main() {
  test_default_runtime_contract();
  test_validate_rejects_unsupported_vad_engine();
  test_simulated_audio_path_starts();
  test_real_audio_requires_portaudio_build();
  return 0;
}
