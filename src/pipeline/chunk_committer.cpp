#include "mev/pipeline/chunk_committer.hpp"

#include <algorithm>
#include <cctype>

namespace mev {

StabilityChunkCommitter::StabilityChunkCommitter(ChunkCommitterConfig config) : config_(config) {}

std::optional<CommittedChunk> StabilityChunkCommitter::on_partial(const AsrPartialHypothesis& partial) {
  const auto candidate = trim_copy(partial.translated_text_en);
  if (candidate.size() < config_.min_chars) {
    return std::nullopt;
  }

  const auto now = Clock::now();

  if (pending_text_ != candidate) {
    pending_text_ = candidate;
    pending_since_ = now;
  }

  const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - pending_since_).count();
  const bool should_commit = partial.end_of_utterance || partial.stability >= config_.stability_threshold ||
                             elapsed_ms >= static_cast<long long>(config_.force_commit_ms);
  if (!should_commit) {
    return std::nullopt;
  }

  std::string speakable = pending_text_;
  if (!last_emitted_accumulated_.empty() &&
      speakable.rfind(last_emitted_accumulated_, 0) == 0) {
    speakable = trim_copy(speakable.substr(last_emitted_accumulated_.size()));
  }

  if (speakable.empty()) {
    return std::nullopt;
  }

  last_emitted_accumulated_ = pending_text_;

  CommittedChunk out;
  out.sequence = partial.sequence;
  out.source_started_at = partial.created_at;
  out.committed_at = now;
  out.text = std::move(speakable);
  return out;
}

void StabilityChunkCommitter::reset() {
  pending_text_.clear();
  last_emitted_accumulated_.clear();
  pending_since_ = TimePoint{};
}

std::string StabilityChunkCommitter::trim_copy(std::string text) {
  text.erase(text.begin(), std::find_if(text.begin(), text.end(), [](unsigned char ch) {
               return std::isspace(ch) == 0;
             }));
  text.erase(std::find_if(text.rbegin(), text.rend(), [](unsigned char ch) {
               return std::isspace(ch) == 0;
             }).base(),
             text.end());
  return text;
}

}  // namespace mev
