#pragma once

#include <atomic>
#include <cstddef>
#include <memory>
#include <thread>

#include "mev/app/i_pipeline_orchestrator.hpp"
#include "mev/asr/i_asr_engine.hpp"
#include "mev/audio/audio_types.hpp"
#include "mev/audio/i_audio_input.hpp"
#include "mev/audio/i_audio_output.hpp"
#include "mev/config/app_config.hpp"
#include "mev/core/spsc_ring_buffer.hpp"
#include "mev/core/utterance.hpp"
#include "mev/domain/i_domain_adapter.hpp"
#include "mev/infra/gpu_scheduler.hpp"
#include "mev/infra/metrics.hpp"
#include "mev/pipeline/tts_scheduler.hpp"
#include "mev/tts/i_tts_engine.hpp"

namespace mev {

// ---------------------------------------------------------------------------
// PipelineMode — graceful degradation state machine.
//
//  NORMAL     : ASR=whisper-small GPU F16, TTS=piper GPU
//  DEGRADED   : ASR=whisper-small CPU Q5_1, TTS=piper CPU
//               Triggered by: GPU inference failure or latency > critical_threshold
//  MINIMAL    : ASR=whisper-tiny CPU Q5_1, TTS=espeak-ng
//               Triggered by: primary model load failure or CPU inference also fails
//  PASSTHROUGH: audio from mic passed directly to virtual output, no processing
//               Triggered by: total failure of all inference backends
// ---------------------------------------------------------------------------
enum class PipelineMode : std::uint8_t {
  NORMAL,
  DEGRADED,
  MINIMAL,
  PASSTHROUGH,
};

// ---------------------------------------------------------------------------
// PipelineOrchestrator — owns and coordinates all 7 pipeline threads.
//
// Thread model (see ARCHITECTURE section in README for full description):
//
//  Thread 0 (RT): Audio Input callback   — PortAudio/simulated
//  Thread 1 (RT): Audio Output callback  — PortAudio/simulated
//  Thread 2:      Ingest Worker          — VAD chunking, builds Utterances
//  Thread 3:      ASR Worker             — Whisper inference
//  Thread 4:      Text Processing Worker — domain correction, TTS scheduling
//  Thread 5:      TTS Worker             — Piper/eSpeak synthesis
//  Thread 6 (low): Supervisor            — watchdog, metrics, health
//
// Queue map (all SPSC, bounded):
//   input_ring_      (RT→T2)  : RawAudioBlock
//   ingest_to_asr_   (T2→T3)  : std::unique_ptr<Utterance>
//   asr_to_text_     (T3→T4)  : std::unique_ptr<Utterance>
//   text_to_tts_     (T4→T5)  : std::unique_ptr<Utterance>
//   tts_to_output_   (T5→RT)  : OutputAudioBlock
// ---------------------------------------------------------------------------
class PipelineOrchestrator final : public IPipelineOrchestrator {
 public:
  explicit PipelineOrchestrator(AppConfig config);
  ~PipelineOrchestrator() override;

  bool start() override;
  void stop() override;
  [[nodiscard]] PipelineState state() const override {
    return state_.load(std::memory_order_acquire);
  }

  [[nodiscard]] MetricsSnapshot metrics_snapshot() const { return metrics_.snapshot(); }
  [[nodiscard]] PipelineMode pipeline_mode() const {
    return mode_.load(std::memory_order_acquire);
  }

  // Signal from warmup: ingest worker may start sending chunks to ASR.
  void set_ready(bool ready) { pipeline_ready_.store(ready, std::memory_order_release); }

 private:
  // ---- Startup helpers --------------------------------------------------
  bool initialize_components();
  bool warmup_models();

  // ---- RT audio callbacks (Thread 0 / Thread 1) -------------------------
  void on_audio_input_callback(const float* input, std::size_t frames, std::uint16_t channels,
                               std::uint32_t sample_rate);
  void on_audio_output_callback(float* output, std::size_t frames, std::uint16_t channels,
                                std::uint32_t sample_rate);

  // ---- Worker loops (Threads 2–6) ---------------------------------------

  // Thread 2: consume RawAudioBlocks → run VAD/windowing → produce Utterances
  void ingest_loop(std::stop_token token);

  // Thread 3: consume Utterances → run ASR → populate translated_text
  void asr_loop(std::stop_token token);

  // Thread 4: domain correction + normalization + TTS scheduling
  // TRADEOFF: text processing is cheap (~1–5ms). Merging Thread 4 into Thread 3
  // would reduce context-switching at the cost of ASR inference sharing a thread
  // with I/O-bound domain lookups. Keep them separate for now.
  void text_loop(std::stop_token token);

  // Thread 5: consume Utterances → run TTS → enqueue OutputAudioBlocks
  void tts_loop(std::stop_token token);

  // Thread 6: health monitoring, latency watchdog, metrics reporting
  void supervisor_loop(std::stop_token token);

  // ---- Degradation ------------------------------------------------------
  void transition_to_mode(PipelineMode mode);
  bool try_activate_tts_on_gpu();  // returns false if contention; TTS falls back to CPU

  // ---- Config & state ---------------------------------------------------
  AppConfig config_;
  std::atomic<PipelineState> state_{PipelineState::kStopped};
  std::atomic<PipelineMode> mode_{PipelineMode::NORMAL};
  std::atomic<bool> pipeline_ready_{false};  // set to true after warmup completes

  // ---- Infrastructure ---------------------------------------------------
  MetricsRegistry metrics_;
  GpuScheduler gpu_scheduler_;

  // ---- SPSC queues (see thread model comment above) ---------------------
  //  input_ring_: SPSC  producer=RT-input-callback  consumer=Thread2
  std::unique_ptr<SpscRingBuffer<RawAudioBlock>> input_ring_;
  //  ingest_to_asr_: SPSC  producer=Thread2  consumer=Thread3
  std::unique_ptr<SpscRingBuffer<std::unique_ptr<Utterance>>> ingest_to_asr_;
  //  asr_to_text_: SPSC  producer=Thread3  consumer=Thread4
  std::unique_ptr<SpscRingBuffer<std::unique_ptr<Utterance>>> asr_to_text_;
  //  text_to_tts_: SPSC  producer=Thread4  consumer=Thread5
  std::unique_ptr<SpscRingBuffer<std::unique_ptr<Utterance>>> text_to_tts_;
  //  tts_to_output_: SPSC  producer=Thread5  consumer=RT-output-callback
  std::unique_ptr<SpscRingBuffer<OutputAudioBlock>> tts_to_output_;

  // ---- Audio & inference backends ---------------------------------------
  std::unique_ptr<IAudioInput>  audio_input_;
  std::unique_ptr<IAudioOutput> audio_output_;
  std::unique_ptr<IASREngine>   asr_engine_;
  std::unique_ptr<ITTSEngine>   tts_engine_;
  std::unique_ptr<ITTSEngine>   tts_fallback_engine_;   // espeak fallback
  std::shared_ptr<class DomainContextManager> domain_context_;
  std::unique_ptr<IDomainAdapter> domain_adapter_;
  std::unique_ptr<TtsScheduler>   tts_scheduler_;

  // ---- Worker threads ---------------------------------------------------
  std::jthread ingest_thread_;      // Thread 2
  std::jthread asr_thread_;         // Thread 3
  std::jthread text_thread_;        // Thread 4
  std::jthread tts_thread_;         // Thread 5
  std::jthread supervisor_thread_;  // Thread 6

  // ---- RT output state (accessed only from RT output callback) ----------
  std::atomic<SequenceNumber> input_sequence_{0};
  OutputAudioBlock current_output_block_{};
  std::size_t current_output_offset_{0};
  bool has_current_output_block_{false};

  // ---- Utterance ID counter (monotonic) ---------------------------------
  std::atomic<std::uint64_t> next_utterance_id_{1};
};

}  // namespace mev
