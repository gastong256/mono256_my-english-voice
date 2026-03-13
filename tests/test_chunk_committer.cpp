#include <cassert>
#include <optional>

#include "mev/pipeline/chunk_committer.hpp"

int main() {
  mev::StabilityChunkCommitter committer(mev::ChunkCommitterConfig{
      .stability_threshold = 0.7F,
      .force_commit_ms = 500,
      .min_chars = 5,
  });

  mev::AsrPartialHypothesis p1;
  p1.sequence = 1;
  p1.created_at = mev::Clock::now();
  p1.translated_text_en = "we need to tune postgresql";
  p1.stability = 0.4F;
  p1.end_of_utterance = false;

  auto result = committer.on_partial(p1);
  assert(!result.has_value());

  p1.stability = 0.85F;
  result = committer.on_partial(p1);
  assert(result.has_value());
  assert(result->text == "we need to tune postgresql");

  p1.translated_text_en = "we need to tune postgresql for latency";
  p1.stability = 0.90F;
  result = committer.on_partial(p1);
  assert(result.has_value());
  assert(result->text == "for latency");

  return 0;
}
