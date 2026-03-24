#pragma once

#include <string>
#include <vector>

#include "mev/tts/i_tts_engine.hpp"

namespace mev {

// ---------------------------------------------------------------------------
// PiperTTSEngine — Piper VITS TTS via ONNX Runtime.
//
// Primary TTS backend for the MVP.
//   Model: en_US-lessac-medium.onnx (or equivalent)
//   Latency: ~50-150ms GPU, ~100-300ms CPU per phrase
//   Output: 22050 Hz mono float32
//
// When MEV_ENABLE_ONNXRUNTIME is OFF, initialize() fails with a clear error so
// callers can explicitly fall back to another backend.
// ---------------------------------------------------------------------------
class PiperTTSEngine final : public ITTSEngine {
 public:
  PiperTTSEngine() = default;
  ~PiperTTSEngine() override { shutdown(); }

  bool initialize(const TTSConfig& config, std::string& error) override;
  void warmup() override;
  [[nodiscard]] bool synthesize(const std::string& text, std::vector<float>& pcm_out) override;
  [[nodiscard]] int output_sample_rate() const override { return output_sample_rate_; }
  [[nodiscard]] std::string engine_name() const override { return "piper"; }
  void shutdown() override;

 private:
  TTSConfig config_{};
  int output_sample_rate_{22050};
  bool initialized_{false};

  // TODO(MEV_ENABLE_ONNXRUNTIME): Ort::Env ort_env_;
  // TODO(MEV_ENABLE_ONNXRUNTIME): Ort::Session session_{nullptr};
};

}  // namespace mev
