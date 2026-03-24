#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#if defined(MEV_ENABLE_ONNXRUNTIME)
#include <onnxruntime_cxx_api.h>
#endif

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
  using PhonemeIdMap = std::unordered_map<char32_t, std::vector<std::int64_t>>;
  using PhonemeMap = std::unordered_map<char32_t, std::vector<char32_t>>;

  PiperTTSEngine() = default;
  ~PiperTTSEngine() override { shutdown(); }

  bool initialize(const TTSConfig& config, std::string& error) override;
  void warmup() override;
  [[nodiscard]] bool synthesize(const std::string& text, std::vector<float>& pcm_out) override;
  [[nodiscard]] bool synthesize_chunk(const SpeechChunk& chunk,
                                      std::vector<float>& pcm_out) override;
  [[nodiscard]] int output_sample_rate() const override { return output_sample_rate_; }
  [[nodiscard]] std::string engine_name() const override { return "piper"; }
  [[nodiscard]] bool gpu_requested() const override { return config_.gpu_enabled; }
  [[nodiscard]] bool using_gpu() const override { return gpu_active_; }
  [[nodiscard]] std::string runtime_summary() const override { return runtime_summary_; }
  void shutdown() override;

 private:
  [[nodiscard]] bool assets_exist(std::string& error) const;
  [[nodiscard]] bool load_voice_config(std::string& error);
  [[nodiscard]] bool load_onnx_session(std::string& error);
  [[nodiscard]] bool text_to_phoneme_ids(const std::string& text,
                                         std::vector<std::int64_t>& phoneme_ids,
                                         std::string& error) const;
  [[nodiscard]] bool synthesize_ids(const std::vector<std::int64_t>& phoneme_ids,
                                    std::vector<float>& pcm_out,
                                    std::string& error);

  TTSConfig config_{};
  int output_sample_rate_{22050};
  bool initialized_{false};
  bool warmed_up_{false};
  bool gpu_active_{false};
  std::string runtime_summary_{"provider=uninitialized"};
  std::string phoneme_type_{"text"};
  std::string espeak_voice_{"en"};
  std::uint32_t num_speakers_{1};
  float noise_scale_{0.667F};
  float length_scale_{1.0F};
  float noise_w_{0.8F};
  PhonemeIdMap phoneme_id_map_;
  PhonemeMap phoneme_map_;

#if defined(MEV_ENABLE_ONNXRUNTIME)
  std::unique_ptr<Ort::Env> ort_env_;
  std::unique_ptr<Ort::Session> session_;
#endif
};

}  // namespace mev
