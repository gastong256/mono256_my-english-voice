#pragma once

#include <atomic>
#include <string>
#include <vector>

#include "mev/asr/i_asr_engine.hpp"

namespace mev {

class WhisperAsrStub final : public IASREngine {
 public:
  WhisperAsrStub(std::string model_path, bool enable_gpu);

  bool warmup(std::string& error) override;
  [[nodiscard]] AsrPartialHypothesis transcribe_incremental(const AsrRequest& request) override;
  [[nodiscard]] std::string name() const override { return "whisper.cpp_stub"; }
  [[nodiscard]] bool gpu_requested() const override { return enable_gpu_; }
  [[nodiscard]] bool using_gpu() const override { return false; }
  [[nodiscard]] std::string runtime_summary() const override;

 private:
  std::string model_path_;
  bool enable_gpu_;
  std::atomic<std::uint64_t> counter_{0};
  std::string last_raw_translation_en_;
  std::string committed_translation_en_;
  std::uint64_t revision_{0};
  std::size_t partial_step_{0};
};

}  // namespace mev
