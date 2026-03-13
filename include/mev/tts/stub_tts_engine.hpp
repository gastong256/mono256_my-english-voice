#pragma once

#include <string>
#include <vector>

#include "mev/tts/i_tts_engine.hpp"

namespace mev {

// ---------------------------------------------------------------------------
// StubTTSEngine — testing stub that generates a short sine wave.
//
// Replaces OnnxTtsStub going forward. Zero heavy dependencies.
// Produces ~1800 samples per word at 22050 Hz to simulate realistic durations.
// ---------------------------------------------------------------------------
class StubTTSEngine final : public ITTSEngine {
 public:
  StubTTSEngine() = default;
  ~StubTTSEngine() override = default;

  bool initialize(const TTSConfig& config, std::string& error) override;
  void warmup() override;
  [[nodiscard]] bool synthesize(const std::string& text, std::vector<float>& pcm_out) override;
  [[nodiscard]] int output_sample_rate() const override { return sample_rate_; }
  [[nodiscard]] std::string engine_name() const override { return "stub"; }
  void shutdown() override;

 private:
  int sample_rate_{22050};
  double phase_{0.0};
  bool initialized_{false};
};

}  // namespace mev
