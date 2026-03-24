#pragma once

#include <memory>
#include <string>
#include <unordered_map>

#include "mev/domain/domain_context_manager.hpp"
#include "mev/domain/i_domain_adapter.hpp"

namespace mev {

// ---------------------------------------------------------------------------
// TechnicalDomainAdapter — concrete IDomainAdapter for tech interview domain.
//
// Pipeline integration:
//   Pre-ASR:  generate_asr_prompt()       → Whisper initial_prompt
//   Post-ASR: correct_asr_output()        → fix ASR mis-transcriptions
//   Post-ASR: update_session_context()    → reinforce domain biasing
//   Pre-TTS:  prepare_for_tts()           → apply pronunciation hints
// ---------------------------------------------------------------------------
class TechnicalDomainAdapter final : public IDomainAdapter {
 public:
  explicit TechnicalDomainAdapter(std::shared_ptr<DomainContextManager> context_manager);

  bool initialize(const DomainConfig& config, std::string& error) override;

  [[nodiscard]] std::string generate_asr_prompt() const override;
  [[nodiscard]] std::string correct_asr_output(const std::string& raw_text) override;
  [[nodiscard]] std::string prepare_for_tts(const std::string& text) override;
  void update_session_context(const std::string& recognized_text) override;
  void reset_session() override;

 private:
  std::shared_ptr<DomainContextManager> context_manager_;

  // ASR correction map: lowercased mis-transcription → correct canonical form.
  // e.g. "post grass" → "PostgreSQL", "cube cuddle" → "kubectl"
  std::unordered_map<std::string, std::string> asr_corrections_;

  // Pronunciation hints for TTS: term → speakable expansion.
  // e.g. "kubectl" → "kube control", "nginx" → "engine X"
  std::unordered_map<std::string, std::string> tts_pronunciation_;
  std::unordered_map<std::string, std::string> preferred_translations_;

  void load_default_corrections();
  void load_default_pronunciation();
  void load_default_preferred_translations();
};

}  // namespace mev
