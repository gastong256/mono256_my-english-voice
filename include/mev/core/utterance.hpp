#pragma once

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

#include "mev/core/types.hpp"
#include "mev/tts/tts_types.hpp"

namespace mev {

// ---------------------------------------------------------------------------
// LatencyMetrics — timestamps at each pipeline stage boundary.
// Each worker stamps its fields when it starts/finishes processing.
// ---------------------------------------------------------------------------
struct LatencyMetrics {
  TimePoint capture_start;    // when the first audio frame of this utterance was captured
  TimePoint capture_end;      // when the last audio frame of the VAD chunk was captured
  TimePoint asr_start;        // when the ASR worker begins inference
  TimePoint asr_end;          // when the ASR worker finishes inference
  TimePoint normalize_end;    // when the text processing worker finishes normalization
  TimePoint tts_start;        // when the TTS worker begins inference
  TimePoint tts_end;          // when the TTS worker finishes synthesis
  TimePoint output_start;     // when the first output audio block is enqueued

  [[nodiscard]] double total_ms() const {
    if (capture_start == TimePoint{} || output_start == TimePoint{}) {
      return 0.0;
    }
    return std::chrono::duration<double, std::milli>(output_start - capture_start).count();
  }

  [[nodiscard]] double asr_ms() const {
    if (asr_start == TimePoint{} || asr_end == TimePoint{}) {
      return 0.0;
    }
    return std::chrono::duration<double, std::milli>(asr_end - asr_start).count();
  }

  [[nodiscard]] double tts_ms() const {
    if (tts_start == TimePoint{} || tts_end == TimePoint{}) {
      return 0.0;
    }
    return std::chrono::duration<double, std::milli>(tts_end - tts_start).count();
  }

  [[nodiscard]] double time_to_first_audio_ms() const {
    return total_ms();
  }
};

// ---------------------------------------------------------------------------
// UtteranceState — lifecycle state machine of one voice utterance.
// ---------------------------------------------------------------------------
enum class UtteranceState : std::uint8_t {
  CAPTURING,           // audio is still being captured (VAD accumulating frames)
  QUEUED_FOR_ASR,      // chunk sent to ingest→ASR queue
  TRANSCRIBING,        // ASR worker is running inference
  COMMITTED,           // ASR produced stable translated text
  NORMALIZING,         // text worker applying domain corrections / pronunciation hints
  QUEUED_FOR_TTS,      // normalized text sent to TTS queue
  SYNTHESIZING,        // TTS worker running inference
  QUEUED_FOR_OUTPUT,   // synthesized PCM enqueued to output ring
  PLAYING,             // audio output callback is consuming PCM frames
  COMPLETED,           // fully played, metrics logged
  DROPPED,             // backlog manager decided to drop this utterance
  FAILED               // any unrecoverable error
};

// ---------------------------------------------------------------------------
// Utterance — central unit of work that flows through the entire pipeline.
//
// Each worker owns the Utterance via unique_ptr while it is active.
// Ownership is transferred via move into the next SPSC queue.
//
// Thread-safety: a single Utterance is owned by exactly ONE thread at a time.
// No shared ownership, no locks needed on the Utterance itself.
// ---------------------------------------------------------------------------
struct Utterance {
  // Monotonic ID. Each Ingest worker chunk increments a global atomic counter.
  std::uint64_t id{0};

  UtteranceState state{UtteranceState::CAPTURING};

  // -- Audio captured by Ingest Worker (16 kHz mono float32) --
  std::vector<float> source_pcm;

  // -- ASR results --
  std::string source_text;      // raw Spanish text (if ASR uses separate transcribe + translate passes)
  std::string translated_text;  // English text from Whisper translate mode
  std::string raw_translated_text;  // full current partial/raw hypothesis
  std::string stable_prefix_text;   // stable text confirmed by the ASR engine
  float asr_stability{0.0F};
  std::uint64_t asr_revision{0};
  bool asr_is_partial{true};
  bool stream_continues{false};

  // -- Text processing results --
  std::string normalized_text;  // after domain correction + pronunciation hints

  // -- TTS result --
  std::vector<float> synth_pcm;  // synthesized PCM (at TTS engine sample rate)
  std::vector<SpeechChunk> speech_chunks;
  bool tts_used_preview_engine{false};

  // -- Scheduling metadata --
  LatencyMetrics metrics;
  bool is_stale{false};  // set by backlog manager; TTS worker skips stale utterances

  // Prompt hint forwarded from DomainContextManager for Whisper initial_prompt
  std::string asr_prompt_hint;

  // -- Accessors for convenience --
  [[nodiscard]] bool is_terminal() const noexcept {
    return state == UtteranceState::COMPLETED || state == UtteranceState::DROPPED ||
           state == UtteranceState::FAILED;
  }
};

}  // namespace mev
