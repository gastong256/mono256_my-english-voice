// test_utterance_lifecycle.cpp
// Tests Utterance struct: state transitions, LatencyMetrics calculations.

#include <cassert>
#include <chrono>
#include <memory>
#include <thread>

#include "mev/core/utterance.hpp"

using namespace mev;

static void test_initial_state() {
  Utterance utt;
  assert(utt.id == 0);
  assert(utt.state == UtteranceState::CAPTURING);
  assert(!utt.is_stale);
  assert(!utt.is_terminal());
  assert(utt.source_pcm.empty());
  assert(utt.translated_text.empty());
  assert(utt.normalized_text.empty());
  assert(utt.synth_pcm.empty());
}

static void test_terminal_states() {
  {
    Utterance utt;
    utt.state = UtteranceState::COMPLETED;
    assert(utt.is_terminal());
  }
  {
    Utterance utt;
    utt.state = UtteranceState::DROPPED;
    assert(utt.is_terminal());
  }
  {
    Utterance utt;
    utt.state = UtteranceState::FAILED;
    assert(utt.is_terminal());
  }
  {
    Utterance utt;
    utt.state = UtteranceState::PLAYING;
    assert(!utt.is_terminal());
  }
}

static void test_metrics_total_ms() {
  Utterance utt;
  const auto t0 = std::chrono::steady_clock::now();
  utt.metrics.capture_start = t0;

  // Simulate 100ms pipeline latency.
  std::this_thread::sleep_for(std::chrono::milliseconds(10));
  utt.metrics.output_start = std::chrono::steady_clock::now();

  const double total = utt.metrics.total_ms();
  assert(total >= 9.0 && "total_ms should be >= 9ms (sleep of 10ms)");
  assert(total < 500.0 && "total_ms should not be unreasonably large");
}

static void test_metrics_asr_ms() {
  Utterance utt;
  utt.metrics.asr_start = std::chrono::steady_clock::now();
  std::this_thread::sleep_for(std::chrono::milliseconds(5));
  utt.metrics.asr_end = std::chrono::steady_clock::now();

  const double asr = utt.metrics.asr_ms();
  assert(asr >= 4.0 && "asr_ms should be >= 4ms");
  assert(asr < 500.0);
}

static void test_metrics_zero_when_unset() {
  Utterance utt;
  assert(utt.metrics.total_ms() == 0.0);
  assert(utt.metrics.asr_ms() == 0.0);
  assert(utt.metrics.tts_ms() == 0.0);
}

static void test_unique_ptr_move() {
  auto utt = std::make_unique<Utterance>();
  utt->id = 42;
  utt->state = UtteranceState::QUEUED_FOR_ASR;
  utt->source_pcm = {1.0F, 2.0F, 3.0F};

  auto utt2 = std::move(utt);
  assert(utt == nullptr);
  assert(utt2->id == 42);
  assert(utt2->state == UtteranceState::QUEUED_FOR_ASR);
  assert(utt2->source_pcm.size() == 3);
}

static void test_full_lifecycle_state_sequence() {
  auto utt = std::make_unique<Utterance>();

  // Walk through expected state transitions.
  utt->state = UtteranceState::CAPTURING;       assert(!utt->is_terminal());
  utt->state = UtteranceState::QUEUED_FOR_ASR;  assert(!utt->is_terminal());
  utt->state = UtteranceState::TRANSCRIBING;    assert(!utt->is_terminal());
  utt->state = UtteranceState::COMMITTED;       assert(!utt->is_terminal());
  utt->state = UtteranceState::NORMALIZING;     assert(!utt->is_terminal());
  utt->state = UtteranceState::QUEUED_FOR_TTS;  assert(!utt->is_terminal());
  utt->state = UtteranceState::SYNTHESIZING;    assert(!utt->is_terminal());
  utt->state = UtteranceState::QUEUED_FOR_OUTPUT; assert(!utt->is_terminal());
  utt->state = UtteranceState::PLAYING;         assert(!utt->is_terminal());
  utt->state = UtteranceState::COMPLETED;       assert(utt->is_terminal());
}

int main() {
  test_initial_state();
  test_terminal_states();
  test_metrics_total_ms();
  test_metrics_asr_ms();
  test_metrics_zero_when_unset();
  test_unique_ptr_move();
  test_full_lifecycle_state_sequence();
  return 0;
}
