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

  if (utterance->asr_is_partial) {
    latest_partial_utterance_id_.store(utterance->id, std::memory_order_release);
  } else {
    latest_final_utterance_id_.store(utterance->id, std::memory_order_release);
  }

  switch (policy_.drop_policy) {
    // ----------------------------------------------------------------
    case DropPolicy::kNone:
      utterance->state = UtteranceState::QUEUED_FOR_TTS;
      return utterance;

    // ----------------------------------------------------------------
    case DropPolicy::kDropOldest:
      if (utterance->asr_is_partial &&
          current_queue_depth >= policy_.partial_backlog_limit) {
        utterance->state = UtteranceState::DROPPED;
        utterance->is_stale = true;
        return nullptr;
      }
      if (current_queue_depth >= policy_.max_queue_depth) {
        utterance->state = UtteranceState::DROPPED;
        utterance->is_stale = true;
        return nullptr;  // caller must count the drop via metrics
      }
      utterance->state = UtteranceState::QUEUED_FOR_TTS;
      return utterance;

    // ----------------------------------------------------------------
    case DropPolicy::kCoalesce: {
      if (utterance->asr_is_partial) {
        if (current_queue_depth >= policy_.partial_backlog_limit) {
          utterance->state = UtteranceState::DROPPED;
          utterance->is_stale = true;
          return nullptr;
        }
        utterance->state = UtteranceState::QUEUED_FOR_TTS;
        return utterance;
      }

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
  if (age_ms > static_cast<long long>(policy_.stale_after_ms)) {
    return true;
  }

  if (utterance.asr_is_partial) {
    const auto newest_partial =
        latest_partial_utterance_id_.load(std::memory_order_acquire);
    const auto newest_final =
        latest_final_utterance_id_.load(std::memory_order_acquire);
    if ((newest_partial > utterance.id) || (newest_final > utterance.id)) {
      return true;
    }
  }

  return false;
}

std::vector<SpeechChunk> TtsScheduler::select_chunks_for_synthesis(
    const Utterance& utterance, const TimePoint now,
    const std::size_t output_queue_depth) const {
  if (utterance.speech_chunks.empty()) {
    return {};
  }

  std::vector<SpeechChunk> selected;
  selected.reserve(utterance.speech_chunks.size());

  const auto slack = std::chrono::milliseconds(
      static_cast<int>(policy_.chunk_deadline_slack_ms));
  for (const auto& chunk : utterance.speech_chunks) {
    if (utterance.asr_is_partial &&
        chunk.deadline_at != TimePoint{} &&
        (now > (chunk.deadline_at + slack))) {
      continue;
    }
    selected.push_back(chunk);
  }

  if (selected.empty()) {
    return selected;
  }

  if (utterance.asr_is_partial &&
      (output_queue_depth >= policy_.output_backlog_limit || selected.size() > 1U)) {
    return {selected.back()};
  }

  return selected;
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
