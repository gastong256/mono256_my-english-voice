#pragma once

#include <string>
#include <vector>

#include "mev/tts/i_tts_engine.hpp"

namespace mev {

// ---------------------------------------------------------------------------
// OnnxTtsStub — legacy stub kept for backward compatibility during transition.
// Prefer StubTTSEngine for new code.
// ---------------------------------------------------------------------------
class OnnxTtsStub final : public ITTSEngine {
 public:
  OnnxTtsStub() = default;
  // Legacy constructor accepting old-style params (no-op for now).
  OnnxTtsStub(std::string model_path, std::string speaker_reference, bool enable_gpu);

  bool initialize(const TTSConfig& config, std::string& error) override;
  void warmup() override;
  [[nodiscard]] bool synthesize(const std::string& text, std::vector<float>& pcm_out) override;
  [[nodiscard]] bool synthesize_chunk(const SpeechChunk& chunk,
                                      std::vector<float>& pcm_out) override;
  [[nodiscard]] int output_sample_rate() const override { return 24000; }
  [[nodiscard]] std::string engine_name() const override { return "onnx_tts_stub"; }
  [[nodiscard]] bool gpu_requested() const override { return false; }
  [[nodiscard]] bool using_gpu() const override { return false; }
  [[nodiscard]] std::string runtime_summary() const override {
    return "provider=stub requested_device=cpu effective_device=cpu";
  }
  void shutdown() override {}

 private:
  std::string model_path_;
  double phase_{0.0};
};

}  // namespace mev
