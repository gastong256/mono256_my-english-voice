#include <cassert>
#include <iostream>
#include <string>

#include "mev/asr/asr_types.hpp"
#include "mev/asr/whisper_asr_stub.hpp"
#include "mev/core/time.hpp"

int main() {
  mev::WhisperAsrStub engine("models/ggml-small.bin", /*enable_gpu=*/false);

  std::string error;
  const bool ok = engine.warmup(error);
  assert(ok && "WhisperAsrStub warmup should succeed with a non-empty path");

  mev::AsrRequest req;
  req.sequence = 1;
  req.created_at = mev::Clock::now();
  req.sample_rate = 16000;
  req.mono_pcm.assign(1600, 0.0F);
  req.stream_continues = true;

  const auto first = engine.transcribe_incremental(req);
  assert(first.is_partial && !first.end_of_utterance);
  assert(first.revision == 1);
  assert(first.translated_text_en.empty() &&
         "the first rolling chunk should stay tentative until the next overlap arrives");

  req.sequence = 2;
  const auto second = engine.transcribe_incremental(req);
  assert(second.is_partial && !second.end_of_utterance);
  assert(second.revision == 2);
  assert(!second.translated_text_en.empty() &&
         "the second rolling chunk should emit a stable partial");
  assert(second.stable_prefix_en == "we need");

  req.sequence = 3;
  const auto third = engine.transcribe_incremental(req);
  assert(third.is_partial && !third.end_of_utterance);
  assert(third.revision == 3);
  assert(!third.translated_text_en.empty());
  assert(third.stable_prefix_en == "we need to review");

  req.sequence = 4;
  req.stream_continues = false;
  const auto final = engine.transcribe_incremental(req);
  assert(!final.is_partial && final.end_of_utterance);
  assert(final.revision == 4);
  assert(!final.translated_text_en.empty());
  assert(final.stability == 1.0F);
  assert(final.raw_translated_text_en.find("backend architecture") != std::string::npos);

  std::cout << "[PASS] test_asr_incremental_contract"
            << " partial2='" << second.translated_text_en << "'"
            << " partial3='" << third.translated_text_en << "'"
            << " final='" << final.translated_text_en << "'\n";
  return 0;
}
