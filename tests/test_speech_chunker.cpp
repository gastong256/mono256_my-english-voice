#include <cassert>
#include <chrono>
#include <iostream>

#include "mev/core/time.hpp"
#include "mev/tts/speech_chunker.hpp"

int main() {
  const auto now = mev::Clock::now();

  const auto final_chunks = mev::chunk_text_for_realtime_tts(
      7, "we need to review the backend architecture, then deploy to production today",
      /*is_partial=*/false, now, /*chunk_budget_ms=*/180);
  assert(final_chunks.size() >= 2 && "long sentences must be split into short speech chunks");
  assert(final_chunks.front().is_partial == false);
  assert(final_chunks.back().is_final && "the last chunk of a final utterance must be final");
  assert(final_chunks.front().deadline_at < final_chunks.back().deadline_at &&
         "deadlines must increase with chunk order");

  const auto partial_chunks = mev::chunk_text_for_realtime_tts(
      8, "we need to reduce latency before release", /*is_partial=*/true, now,
      /*chunk_budget_ms=*/120);
  assert(!partial_chunks.empty());
  assert(partial_chunks.front().is_partial);
  assert(!partial_chunks.back().is_final &&
         "partial utterances must not mark any chunk as final");

  std::cout << "[PASS] test_speech_chunker"
            << " final_chunks=" << final_chunks.size()
            << " partial_chunks=" << partial_chunks.size() << "\n";
  return 0;
}
