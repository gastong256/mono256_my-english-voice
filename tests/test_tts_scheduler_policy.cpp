// test_tts_scheduler_policy.cpp
// Tests TtsScheduler drop policies: drop_oldest, coalesce, none.
// Also tests stale detection.

#include <cassert>
#include <chrono>
#include <memory>
#include <string>
#include <thread>

#include "mev/core/utterance.hpp"
#include "mev/pipeline/tts_scheduler.hpp"

using namespace mev;

static std::unique_ptr<Utterance> make_utt(std::uint64_t id, const std::string& text) {
  auto u = std::make_unique<Utterance>();
  u->id = id;
  u->normalized_text = text;
  u->metrics.capture_start = std::chrono::steady_clock::now();
  u->state = UtteranceState::NORMALIZING;
  return u;
}

// --- DropPolicy::kNone -------------------------------------------------------
static void test_none_always_passes() {
  TtsScheduler sched(TtsSchedulerPolicy{
      .max_queue_depth    = 4,
      .backlog_soft_limit = 2,
      .stale_after_ms     = 3000,
      .drop_policy        = DropPolicy::kNone,
  });

  // Even at over-capacity, kNone should pass everything through.
  for (int i = 0; i < 10; ++i) {
    auto result = sched.schedule(make_utt(static_cast<std::uint64_t>(i), "hello"), 99);
    assert(result != nullptr && "kNone must always return utterance");
    assert(result->state == UtteranceState::QUEUED_FOR_TTS);
  }
}

// --- DropPolicy::kDropOldest -------------------------------------------------
static void test_drop_oldest_under_limit() {
  TtsScheduler sched(TtsSchedulerPolicy{
      .max_queue_depth    = 4,
      .backlog_soft_limit = 2,
      .stale_after_ms     = 3000,
      .drop_policy        = DropPolicy::kDropOldest,
  });

  // Below max_queue_depth — should pass.
  for (std::size_t depth = 0; depth < 4; ++depth) {
    auto result = sched.schedule(make_utt(depth, "text"), depth);
    assert(result != nullptr);
    assert(result->state == UtteranceState::QUEUED_FOR_TTS);
  }
}

static void test_drop_oldest_at_limit() {
  TtsScheduler sched(TtsSchedulerPolicy{
      .max_queue_depth    = 4,
      .backlog_soft_limit = 2,
      .stale_after_ms     = 3000,
      .drop_policy        = DropPolicy::kDropOldest,
  });

  // At or beyond max_queue_depth — should drop (return nullptr).
  auto result = sched.schedule(make_utt(99, "text"), 4 /* == max_queue_depth */);
  assert(result == nullptr && "kDropOldest must return nullptr when at capacity");
}

// --- DropPolicy::kCoalesce ---------------------------------------------------
static void test_coalesce_buffers_until_drained() {
  TtsScheduler sched(TtsSchedulerPolicy{
      .max_queue_depth    = 8,
      .backlog_soft_limit = 2,
      .stale_after_ms     = 3000,
      .drop_policy        = DropPolicy::kCoalesce,
  });

  // Above soft_limit: should buffer, not emit.
  for (int i = 0; i < 3; ++i) {
    auto result = sched.schedule(make_utt(static_cast<std::uint64_t>(i), "word"), 5 /*> soft_limit=2*/);
    assert(result == nullptr && "kCoalesce should buffer when queue > soft_limit");
  }
  assert(sched.coalesce_buffer_size() == 3);

  // Below soft_limit: should flush coalesced utterance.
  auto result = sched.schedule(make_utt(10, "extra"), 1 /*< soft_limit=2*/);
  assert(result != nullptr && "kCoalesce should flush when queue drops below soft_limit");
  assert(result->normalized_text.find("word") != std::string::npos && "coalesced text must contain buffered words");
  assert(sched.coalesce_buffer_size() == 0);
}

static void test_coalesce_flush_explicit() {
  TtsScheduler sched(TtsSchedulerPolicy{
      .max_queue_depth    = 8,
      .backlog_soft_limit = 2,
      .stale_after_ms     = 3000,
      .drop_policy        = DropPolicy::kCoalesce,
  });

  // Nothing buffered — flush returns nullptr.
  assert(sched.flush_coalesced() == nullptr);

  // Buffer one item, then flush explicitly.
  (void)sched.schedule(make_utt(1, "hello"), 5);
  auto flushed = sched.flush_coalesced();
  assert(flushed != nullptr);
  assert(flushed->normalized_text == "hello");
}

// --- Stale detection ---------------------------------------------------------
static void test_stale_detection_by_age() {
  TtsScheduler sched(TtsSchedulerPolicy{
      .max_queue_depth    = 8,
      .backlog_soft_limit = 4,
      .stale_after_ms     = 100,  // very short for testing
      .drop_policy        = DropPolicy::kNone,
  });

  auto utt = make_utt(1, "test");
  utt->metrics.capture_start = std::chrono::steady_clock::now() - std::chrono::milliseconds(200);

  assert(sched.should_cancel_as_stale(*utt, std::chrono::steady_clock::now()) &&
         "utterance older than stale_after_ms must be stale");
}

static void test_stale_detection_fresh() {
  TtsScheduler sched(TtsSchedulerPolicy{
      .max_queue_depth    = 8,
      .backlog_soft_limit = 4,
      .stale_after_ms     = 3000,
      .drop_policy        = DropPolicy::kNone,
  });

  auto utt = make_utt(1, "fresh");
  // capture_start is just now — should not be stale.
  assert(!sched.should_cancel_as_stale(*utt, std::chrono::steady_clock::now()) &&
         "freshly created utterance must not be stale");
}

static void test_stale_flag_override() {
  TtsScheduler sched(TtsSchedulerPolicy{
      .max_queue_depth    = 8,
      .backlog_soft_limit = 4,
      .stale_after_ms     = 3000,
      .drop_policy        = DropPolicy::kNone,
  });

  auto utt = make_utt(1, "fresh");
  utt->is_stale = true;  // explicitly marked stale
  assert(sched.should_cancel_as_stale(*utt, std::chrono::steady_clock::now()) &&
         "is_stale=true must always trigger cancellation");
}

int main() {
  test_none_always_passes();
  test_drop_oldest_under_limit();
  test_drop_oldest_at_limit();
  test_coalesce_buffers_until_drained();
  test_coalesce_flush_explicit();
  test_stale_detection_by_age();
  test_stale_detection_fresh();
  test_stale_flag_override();
  return 0;
}
