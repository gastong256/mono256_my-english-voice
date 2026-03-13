#include "mev/pipeline/tts_scheduler.hpp"

#include <chrono>
#include <numeric>
#include <sstream>

namespace mev {

TtsScheduler::TtsScheduler(TtsSchedulerPolicy policy) : policy_(std::move(policy)) {}

std::unique_ptr<Utterance> TtsScheduler::schedule(std::unique_ptr<Utterance> utterance,
                                                    const std::size_t current_queue_depth) {
  if (!utterance) {
    return nullptr;
  }

  switch (policy_.drop_policy) {
    // ----------------------------------------------------------------
    case DropPolicy::kNone:
      utterance->state = UtteranceState::QUEUED_FOR_TTS;
      return utterance;

    // ----------------------------------------------------------------
    case DropPolicy::kDropOldest:
      if (current_queue_depth >= policy_.max_queue_depth) {
        utterance->state = UtteranceState::DROPPED;
        utterance->is_stale = true;
        return nullptr;  // caller must count the drop via metrics
      }
      utterance->state = UtteranceState::QUEUED_FOR_TTS;
      return utterance;

    // ----------------------------------------------------------------
    case DropPolicy::kCoalesce: {
      // Buffer utterances until the queue drains below the soft limit.
      coalesce_buffer_.push_back(std::move(utterance));

      if (current_queue_depth < policy_.backlog_soft_limit ||
          coalesce_buffer_.size() >= policy_.max_queue_depth) {
        return flush_coalesced();
      }
      // Not ready yet: held in buffer.
      return nullptr;
    }
  }

  // Unreachable — keep compiler happy.
  utterance->state = UtteranceState::QUEUED_FOR_TTS;
  return utterance;
}

bool TtsScheduler::should_cancel_as_stale(const Utterance& utterance,
                                           const TimePoint now) const {
  if (utterance.is_stale) {
    return true;
  }
  if (utterance.metrics.capture_start == TimePoint{}) {
    return false;  // no timestamp set — cannot determine staleness
  }
  const auto age_ms =
      std::chrono::duration_cast<std::chrono::milliseconds>(now - utterance.metrics.capture_start)
          .count();
  return age_ms > static_cast<long long>(policy_.stale_after_ms);
}

std::unique_ptr<Utterance> TtsScheduler::flush_coalesced() {
  if (coalesce_buffer_.empty()) {
    return nullptr;
  }

  if (coalesce_buffer_.size() == 1U) {
    auto single = std::move(coalesce_buffer_.front());
    coalesce_buffer_.clear();
    single->state = UtteranceState::QUEUED_FOR_TTS;
    return single;
  }

  // Merge all buffered utterances into the first one.
  auto merged = std::move(coalesce_buffer_.front());
  std::ostringstream combined_text;
  combined_text << merged->normalized_text;

  for (std::size_t i = 1; i < coalesce_buffer_.size(); ++i) {
    if (!coalesce_buffer_[i]->normalized_text.empty()) {
      combined_text << ' ' << coalesce_buffer_[i]->normalized_text;
    }
    // Accumulate source PCM from all chunks
    merged->source_pcm.insert(merged->source_pcm.end(), coalesce_buffer_[i]->source_pcm.begin(),
                               coalesce_buffer_[i]->source_pcm.end());
  }

  merged->normalized_text = combined_text.str();
  merged->state = UtteranceState::QUEUED_FOR_TTS;
  coalesce_buffer_.clear();
  return merged;
}

}  // namespace mev
