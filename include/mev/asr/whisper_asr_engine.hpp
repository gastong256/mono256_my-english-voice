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
  WhisperASREngine(const std::string& model_path, bool enable_gpu);
  ~WhisperASREngine() override;

  bool warmup(std::string& error) override;
  [[nodiscard]] AsrPartialHypothesis transcribe_incremental(const AsrRequest& request) override;
  [[nodiscard]] std::string name() const override { return "whisper.cpp"; }

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
  std::string domain_prompt_;
  std::atomic<std::uint64_t> inference_count_{0};
};

}  // namespace mev
