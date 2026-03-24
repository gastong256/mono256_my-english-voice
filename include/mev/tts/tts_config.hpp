#pragma once

#include <cstdint>
#include <string>

namespace mev {

// Configuration passed to ITTSEngine::initialize().
// Derived from the [tts] section of the TOML pipeline config.
struct TTSConfig {
  std::string engine{"stub"};           // "piper", "espeak", "stub"
  std::string mode{"interactive_balanced"};  // "interactive_preview", "interactive_balanced"
  std::string model_path;               // path to ONNX model file (Piper)
  std::string piper_data_path;          // path to Piper JSON companion file
  std::uint32_t speaker_id{0};          // speaker index (Piper multi-speaker models)
  bool gpu_enabled{true};               // use CUDA execution provider if available
  std::uint32_t output_sample_rate{22050};  // Hz; Piper default
  std::string fallback_engine{"espeak"};    // engine name to fall back to on init failure
  std::string preview_engine{"espeak"};     // low-latency preview backend
  std::uint32_t max_primary_tts_budget_ms{180};
};

}  // namespace mev
