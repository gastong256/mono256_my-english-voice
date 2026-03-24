#pragma once

#include <atomic>
#include <string>
#include <vector>

#include "mev/asr/i_asr_engine.hpp"

#if defined(MEV_ENABLE_WHISPER_CPP)
#include <whisper.h>
#endif

namespace mev {

// ---------------------------------------------------------------------------
// WhisperASREngine — whisper.cpp ASR backend.
//
// Wraps whisper.cpp for Spanish -> English translation with domain prompting
// and sliding context token injection.
//
// When MEV_ENABLE_WHISPER_CPP is OFF, this compiles as a stub that logs
// "whisper.cpp not compiled in" and returns an empty hypothesis.
// ---------------------------------------------------------------------------
class WhisperASREngine final : public IASREngine {
 public:
  WhisperASREngine(const std::string& model_path, bool enable_gpu,
                   std::string language = "es", bool translate = true,
                   std::string quantization = "f16");
  ~WhisperASREngine() override;

  bool warmup(std::string& error) override;
  [[nodiscard]] AsrPartialHypothesis transcribe_incremental(const AsrRequest& request) override;
  [[nodiscard]] std::string name() const override { return "whisper.cpp"; }
  [[nodiscard]] bool gpu_requested() const override { return enable_gpu_; }
  [[nodiscard]] bool using_gpu() const override { return gpu_active_; }
  [[nodiscard]] std::string runtime_summary() const override { return runtime_summary_; }

  // Update the ASR domain prompt (called before inference from asr_loop).
  void set_domain_prompt(const std::string& prompt);

  // Clear sliding context token state (call between sessions).
  void reset_context();

 private:
#if defined(MEV_ENABLE_WHISPER_CPP)
  whisper_context* ctx_{nullptr};
  std::vector<whisper_token> prev_tokens_;  // sliding context window
#endif
  std::string model_path_;
  bool enable_gpu_;
  std::string language_;
  bool translate_;
  std::string quantization_;
  std::string domain_prompt_;
  bool gpu_active_{false};
  bool warmed_up_{false};
  std::string runtime_summary_{"provider=uninitialized"};
  std::atomic<std::uint64_t> inference_count_{0};
  std::string last_raw_translation_en_;
  std::string committed_translation_en_;
  std::uint64_t revision_{0};
  std::size_t repeated_hypothesis_count_{0};
};

}  // namespace mev
