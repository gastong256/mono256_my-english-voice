#include "mev/tts/piper_tts_engine.hpp"

#include <filesystem>

#include "mev/core/logger.hpp"

// TODO(MEV_ENABLE_ONNXRUNTIME): #include <onnxruntime_cxx_api.h>

namespace mev {

bool PiperTTSEngine::initialize(const TTSConfig& config, std::string& error) {
  config_ = config;
  output_sample_rate_ = static_cast<int>(config.output_sample_rate);

#if defined(MEV_ENABLE_ONNXRUNTIME)
  // Real implementation: load Piper ONNX model via ONNX Runtime.
  // Steps:
  //   1. Create Ort::Env
  //   2. Set SessionOptions (CUDA EP if config.gpu_enabled)
  //   3. Load config.model_path into session_
  //   4. Validate input/output tensor names against Piper model spec
  //   5. Read phoneme map from config.piper_data_path
  (void)error;
  MEV_LOG_ERROR("PiperTTSEngine: ONNX Runtime not compiled in (MEV_ENABLE_ONNXRUNTIME=OFF)");
  return false;
#else
  error = "PiperTTSEngine: ONNX Runtime not compiled in (MEV_ENABLE_ONNXRUNTIME=OFF)";
  MEV_LOG_ERROR(error);
  return false;
#endif
}

void PiperTTSEngine::warmup() {
  if (!initialized_) {
    return;
  }
  // TODO(MEV_ENABLE_ONNXRUNTIME): run a dummy phoneme through the ONNX session.
  MEV_LOG_INFO("PiperTTSEngine warmup skipped (stub mode)");
}

bool PiperTTSEngine::synthesize(const std::string& text, std::vector<float>& pcm_out) {
  if (!initialized_) {
    return false;
  }

#if defined(MEV_ENABLE_ONNXRUNTIME)
  // Real Piper inference path:
  //   1. Text → phonemes (using espeak-ng phonemizer or Piper's built-in)
  //   2. Phonemes → phoneme IDs via Piper phoneme_id_map
  //   3. Run ONNX session: input = phoneme IDs + lengths + speaker ID
  //   4. Output = mel spectrogram → vocoder → PCM
  //   5. Fill pcm_out at output_sample_rate_ Hz
  (void)text;
  pcm_out.clear();
  return false;
#else
  (void)text;
  pcm_out.clear();
  return false;
#endif
}

void PiperTTSEngine::shutdown() {
  initialized_ = false;
  // TODO(MEV_ENABLE_ONNXRUNTIME): release session_ and ort_env_
}

}  // namespace mev
