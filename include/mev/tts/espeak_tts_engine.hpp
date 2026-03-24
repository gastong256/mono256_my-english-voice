#pragma once

#include <string>
#include <vector>

#include "mev/tts/i_tts_engine.hpp"

namespace mev {

// ---------------------------------------------------------------------------
// EspeakTTSEngine — eSpeak-ng fallback TTS.
//
// Ultra-fast (~5ms per phrase), robotic quality, no GPU dependency.
// Used in MINIMAL degradation mode when Piper fails to load.
//
// When MEV_ENABLE_ESPEAK is OFF, initialize() fails with a clear error so
// callers can fall back to another backend or surface an actionable message.
// ---------------------------------------------------------------------------
class EspeakTTSEngine final : public ITTSEngine {
 public:
  EspeakTTSEngine() = default;
  ~EspeakTTSEngine() override { shutdown(); }

  bool initialize(const TTSConfig& config, std::string& error) override;
  void warmup() override;
  [[nodiscard]] bool synthesize(const std::string& text, std::vector<float>& pcm_out) override;
  [[nodiscard]] int output_sample_rate() const override { return sample_rate_; }
  [[nodiscard]] std::string engine_name() const override { return "espeak"; }
  void shutdown() override;

 private:
  bool initialized_{false};
  int sample_rate_{22050};
};

}  // namespace mev
