#include <cassert>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#include "mev/asr/whisper_asr_engine.hpp"
#include "mev/asr/asr_types.hpp"
#include "mev/core/time.hpp"

int main() {
#if !defined(MEV_ENABLE_WHISPER_CPP)
  std::cout << "[SKIP] test_whisper_asr: MEV_ENABLE_WHISPER_CPP=OFF\n";
  return 0;
#else
  const std::string model_path = "models/ggml-small.bin";
  if (!std::filesystem::exists(model_path)) {
    std::cout << "[SKIP] test_whisper_asr: model not found at '" << model_path << "'\n";
    return 0;
  }

  mev::WhisperASREngine engine(model_path, /*enable_gpu=*/false);

  std::string warmup_error;
  const bool ok = engine.warmup(warmup_error);
  if (!ok) {
    std::cerr << "[FAIL] test_whisper_asr: warmup failed: " << warmup_error << "\n";
    return 1;
  }

  // Feed 1 second of silence at 16kHz (16000 samples).
  mev::AsrRequest req;
  req.sequence    = 1;
  req.created_at  = mev::Clock::now();
  req.sample_rate = 16000;
  req.mono_pcm.assign(16000, 0.0F);

  const auto result = engine.transcribe_incremental(req);
  // Should not crash; result may be empty for silence.
  assert(result.end_of_utterance && "silence must mark end_of_utterance=true");

  // Test set_domain_prompt and reset_context don't crash.
  engine.set_domain_prompt("Kubernetes PostgreSQL FastAPI");
  engine.reset_context();

  std::cout << "[PASS] test_whisper_asr: warmup OK, silence transcription OK"
            << " (text='" << result.translated_text_en << "')\n";
  return 0;
#endif
}
