#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>

#include "mev/core/types.hpp"
#include "mev/core/utterance.hpp"
#include "mev/tts/tts_types.hpp"

namespace mev {

// ---------------------------------------------------------------------------
// DropPolicy — backlog management strategy for the TTS queue.
// ---------------------------------------------------------------------------
enum class DropPolicy : std::uint8_t {
  kNone,        // process all utterances in order (testing only)
  kDropOldest,  // when queue >= max_depth, mark oldest as DROPPED
  kCoalesce,    // concatenate multiple pending utterances into one TTS call
};

// ---------------------------------------------------------------------------
// TtsSchedulerPolicy — tuning parameters for backlog management.
// Maps to [pipeline] section of pipeline.toml.
// ---------------------------------------------------------------------------
struct TtsSchedulerPolicy {
  std::size_t max_queue_depth{8};         // hard cap: above this, apply drop_policy
  std::size_t backlog_soft_limit{4};      // above this: apply truncation hints
  std::size_t partial_backlog_limit{2};   // partials are dropped more aggressively
  std::size_t output_backlog_limit{6};    // pending audio blocks beyond this => prefer latest chunk only
  std::uint32_t stale_after_ms{3000};     // utterance older than this → stale
  std::uint32_t stale_after_n_newer{3};   // drop if N newer utterances exist in queue
  std::uint32_t chunk_deadline_slack_ms{40};  // allow small scheduling jitter for chunk deadlines

  // TRADEOFF: kCoalesce reduces TTS calls but can increase utterance latency;
  // kDropOldest keeps latency bounded at the cost of dropped phrases.
  DropPolicy drop_policy{DropPolicy::kDropOldest};
};

// ---------------------------------------------------------------------------
// TtsScheduler — decides whether an Utterance should be synthesized and in
// what order. Owns the pending-utterance coalesce buffer.
//
// Thread: called exclusively from Thread 4 (Text Processing Worker).
// ---------------------------------------------------------------------------
class TtsScheduler {
 public:
  explicit TtsScheduler(TtsSchedulerPolicy policy);

  // Evaluate whether utterance should be dispatched to TTS.
  //  - kNone: always returns the utterance as-is.
  //  - kDropOldest: if current_queue_depth >= max_queue_depth, marks utterance
  //    as DROPPED and returns nullptr.
  //  - kCoalesce: buffers utterances; returns coalesced utterance when
  //    current_queue_depth drops below soft_limit, else returns nullptr.
  //
  // Ownership of the returned Utterance is transferred to the caller.
  [[nodiscard]] std::unique_ptr<Utterance> schedule(std::unique_ptr<Utterance> utterance,
                                                     std::size_t current_queue_depth);

  // Returns true if the utterance should be skipped by the TTS worker.
  // Called immediately before synthesize() in the TTS worker loop.
  [[nodiscard]] bool should_cancel_as_stale(const Utterance& utterance, TimePoint now) const;

  // Returns the speech chunks that should still be synthesized for this utterance.
  // In interactive mode, stale or overdue partial chunks are skipped and the newest
  // surviving partial chunk is preferred when output backlog is high.
  [[nodiscard]] std::vector<SpeechChunk> select_chunks_for_synthesis(
      const Utterance& utterance, TimePoint now, std::size_t output_queue_depth) const;

  // kCoalesce mode: flush buffered utterances into one if non-empty.
  [[nodiscard]] std::unique_ptr<Utterance> flush_coalesced();

  [[nodiscard]] std::size_t coalesce_buffer_size() const { return coalesce_buffer_.size(); }

 private:
  TtsSchedulerPolicy policy_;
  std::atomic<std::uint64_t> latest_partial_utterance_id_{0};
  std::atomic<std::uint64_t> latest_final_utterance_id_{0};

  // Pending coalesce buffer — used only in kCoalesce mode.
  std::vector<std::unique_ptr<Utterance>> coalesce_buffer_;
};

}  // namespace mev
