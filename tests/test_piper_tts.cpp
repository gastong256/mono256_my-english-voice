#include <cassert>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#include "mev/tts/piper_tts_engine.hpp"
#include "mev/tts/tts_config.hpp"

int main() {
#if !defined(MEV_ENABLE_ONNXRUNTIME)
  std::cout << "[SKIP] test_piper_tts: MEV_ENABLE_ONNXRUNTIME=OFF\n";
  return 0;
#else
  const std::string model_path = "models/en_US-lessac-medium.onnx";
  const std::string json_path  = "models/en_US-lessac-medium.onnx.json";

  if (!std::filesystem::exists(model_path) || !std::filesystem::exists(json_path)) {
    std::cout << "[SKIP] test_piper_tts: model files not found\n";
    return 0;
  }

  mev::TTSConfig cfg{};
  cfg.engine             = "piper";
  cfg.model_path         = model_path;
  cfg.piper_data_path    = json_path;
  cfg.output_sample_rate = 22050;
  cfg.gpu_enabled        = false;  // Use CPU for test

  mev::PiperTTSEngine engine;
  std::string error;
  const bool init_ok = engine.initialize(cfg, error);
  if (!init_ok) {
    std::cerr << "[FAIL] test_piper_tts: initialize failed: " << error << "\n";
    return 1;
  }

  engine.warmup();

  std::vector<float> pcm;
  const bool synth_ok = engine.synthesize("Hello", pcm);
  if (!synth_ok) {
    std::cerr << "[FAIL] test_piper_tts: synthesize returned false\n";
    return 1;
  }
  assert(!pcm.empty() && "synthesize must produce non-empty PCM");
  assert(engine.output_sample_rate() == 22050 && "sample rate must be 22050");

  engine.shutdown();

  std::cout << "[PASS] test_piper_tts: synthesized " << pcm.size()
            << " samples at " << engine.output_sample_rate() << " Hz\n";
  return 0;
#endif
}
