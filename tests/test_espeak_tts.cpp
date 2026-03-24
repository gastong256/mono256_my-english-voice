#include <cassert>
#include <iostream>
#include <string>
#include <vector>

#include "mev/tts/espeak_tts_engine.hpp"
#include "mev/tts/tts_config.hpp"

int main() {
#if !defined(MEV_ENABLE_ESPEAK)
  std::cout << "[SKIP] test_espeak_tts: MEV_ENABLE_ESPEAK=OFF\n";
  return 0;
#else
  mev::TTSConfig cfg{};
  cfg.engine = "espeak";

  mev::EspeakTTSEngine engine;
  std::string error;
  const bool init_ok = engine.initialize(cfg, error);
  if (!init_ok) {
    std::cerr << "[FAIL] test_espeak_tts: initialize failed: " << error << "\n";
    return 1;
  }

  engine.warmup();

  std::vector<float> pcm;
  const bool synth_ok = engine.synthesize("Hello from eSpeak", pcm);
  if (!synth_ok) {
    std::cerr << "[FAIL] test_espeak_tts: synthesize returned false\n";
    engine.shutdown();
    return 1;
  }

  assert(!pcm.empty() && "eSpeak must produce non-empty PCM");
  assert(engine.output_sample_rate() > 0 && "sample rate must be positive");

  engine.shutdown();

  std::cout << "[PASS] test_espeak_tts: synthesized " << pcm.size()
            << " samples at " << engine.output_sample_rate() << " Hz\n";
  return 0;
#endif
}
