#pragma once

#include <string>

#include "mev/config/app_config.hpp"

namespace mev {

// ---------------------------------------------------------------------------
// IDomainAdapter — interface for domain-specific text processing.
//
// Pipeline integration points:
//   Pre-ASR:  generate_asr_prompt()          → Whisper initial_prompt
//   Post-ASR: correct_asr_output(raw)        → fix ASR transcription errors
//   Post-ASR: update_session_context(text)   → reinforce domain biasing
//   Pre-TTS:  prepare_for_tts(text)          → apply pronunciation hints
// ---------------------------------------------------------------------------
class IDomainAdapter {
 public:
  virtual ~IDomainAdapter() = default;

  // Load glossary, correction maps, and pronunciation hints from config.
  // Called once during pipeline startup.
  virtual bool initialize(const DomainConfig& config, std::string& error) = 0;

  // Build the Whisper initial_prompt string from glossary + session terms.
  // Called by the Ingest worker before each ASR chunk.
  [[nodiscard]] virtual std::string generate_asr_prompt() const = 0;

  // Apply post-ASR corrections: fix common ASR mis-transcriptions of
  // technical terms (e.g. "post-grass" → "PostgreSQL").
  // Called by the Text Processing worker after ASR returns.
  [[nodiscard]] virtual std::string correct_asr_output(const std::string& raw_text) = 0;

  // Apply pronunciation hints for TTS: expand abbreviations and technical
  // terms into speakable forms (e.g. "kubectl" → "kube control").
  // Called by the Text Processing worker before TTS.
  [[nodiscard]] virtual std::string prepare_for_tts(const std::string& text) = 0;

  // Update session context with recognized technical terms from recent output.
  // Reinforces domain biasing for subsequent Whisper calls.
  virtual void update_session_context(const std::string& recognized_text) = 0;

  // Clear session-specific term accumulation (e.g. on topic change).
  virtual void reset_session() = 0;
};

}  // namespace mev
