#include <cassert>
#include <iostream>
#include <string>
#include <vector>

#include "mev/tts/espeak_tts_engine.hpp"
#include "mev/tts/piper_tts_engine.hpp"
#include "mev/tts/tts_config.hpp"

int main() {
  mev::TTSConfig piper_cfg{};
  piper_cfg.engine = "piper";
  piper_cfg.model_path = "models/definitely-missing-piper.onnx";
  piper_cfg.piper_data_path = "models/definitely-missing-piper.onnx.json";
  piper_cfg.output_sample_rate = 22050;
  piper_cfg.gpu_enabled = false;

  mev::PiperTTSEngine piper;
  std::string piper_error;
  const bool piper_ok = piper.initialize(piper_cfg, piper_error);
  assert(!piper_ok && "Piper must fail clearly when its model assets are missing");
  assert(!piper_error.empty() && "Piper failure must provide an actionable error");

  mev::TTSConfig espeak_cfg{};
  espeak_cfg.engine = "espeak";
  espeak_cfg.output_sample_rate = 22050;

  mev::EspeakTTSEngine espeak;
  std::string espeak_error;
  const bool espeak_ok = espeak.initialize(espeak_cfg, espeak_error);
  assert(espeak_ok && "eSpeak must initialize as the fallback backend when compiled in");

  std::vector<float> pcm;
  const bool synth_ok = espeak.synthesize("Fallback path is operational", pcm);
  assert(synth_ok && "eSpeak fallback must synthesize audio");
  assert(!pcm.empty() && "eSpeak fallback must produce non-empty PCM");

  espeak.shutdown();

  std::cout << "[PASS] test_tts_fallback_contract (piper_error='" << piper_error
            << "', samples=" << pcm.size() << ")\n";
  return 0;
}
