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
// Implementation status: STUB — returns silence until libespeak-ng is linked.
//
// To activate: link espeak-ng and implement the synthesis call in .cpp.
// ---------------------------------------------------------------------------
class EspeakTTSEngine final : public ITTSEngine {
 public:
  EspeakTTSEngine() = default;
  ~EspeakTTSEngine() override { shutdown(); }

  bool initialize(const TTSConfig& config, std::string& error) override;
  void warmup() override;
  [[nodiscard]] bool synthesize(const std::string& text, std::vector<float>& pcm_out) override;
  [[nodiscard]] int output_sample_rate() const override { return 16000; }
  [[nodiscard]] std::string engine_name() const override { return "espeak"; }
  void shutdown() override;

 private:
  bool initialized_{false};
  int sample_rate_{22050};
};

}  // namespace mev
