#include "mev/pipeline/pipeline_orchestrator.hpp"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <memory>
#include <thread>

#include "mev/asr/whisper_asr_stub.hpp"
#include "mev/audio/null_vad_engine.hpp"
#include "mev/audio/resampler.hpp"
#include "mev/audio/simulated_audio_input.hpp"
#include "mev/audio/simulated_audio_output.hpp"
#include "mev/audio/webrtc_vad_engine.hpp"
#include "mev/core/logger.hpp"
#include "mev/core/profiling.hpp"
#include "mev/domain/domain_context_manager.hpp"
#include "mev/domain/technical_domain_adapter.hpp"
#include "mev/infra/metrics.hpp"
#include "mev/tts/espeak_tts_engine.hpp"
#include "mev/tts/piper_tts_engine.hpp"
#include "mev/tts/speech_chunker.hpp"
#include "mev/tts/stub_tts_engine.hpp"

#if defined(MEV_ENABLE_PORTAUDIO)
#include "mev/audio/portaudio_input.hpp"
#include "mev/audio/portaudio_output.hpp"
#endif

#if defined(MEV_ENABLE_WHISPER_CPP)
#include "mev/asr/whisper_asr_engine.hpp"
#endif

// ---------------------------------------------------------------------------
// Thread model (7 threads):
//   Thread 0 (RT): on_audio_input_callback  — bounded copy, atomic counter only
//   Thread 1 (RT): on_audio_output_callback — bounded dequeue, silence on underrun
//   Thread 2:      ingest_loop              — VAD/windowing, builds Utterances
//   Thread 3:      asr_loop                 — Whisper inference, GPU-priority
//   Thread 4:      text_loop                — domain correction + TTS scheduling
//     TRADEOFF: text_loop is cheap (~1-5ms). Merging into Thread 3 saves one
//     context switch but couples ASR inference with I/O-bound domain lookups.
//     Keeping them separate is safer for MVP.
//   Thread 5:      tts_loop                 — Piper/eSpeak synthesis
//   Thread 6 (low): supervisor_loop         — watchdog, degradation, metrics
//
// Queue map (all SPSC, bounded, overflow=drop+metric):
//   input_ring_    : RawAudioBlock         T0→T2
//   ingest_to_asr_ : unique_ptr<Utterance> T2→T3
//   asr_to_text_   : unique_ptr<Utterance> T3→T4
//   text_to_tts_   : unique_ptr<Utterance> T4→T5
//   tts_to_output_ : OutputAudioBlock      T5→T1
// ---------------------------------------------------------------------------

namespace mev {

namespace {

constexpr std::size_t kSleepMicros = 500;

std::unique_ptr<ITTSEngine> make_tts_engine(const std::string& engine_name,
                                             const TtsConfig& cfg, std::string& error) {
  std::unique_ptr<ITTSEngine> engine;
  if (engine_name == "piper") {
    engine = std::make_unique<PiperTTSEngine>();
  } else if (engine_name == "espeak") {
    engine = std::make_unique<EspeakTTSEngine>();
  } else {
    engine = std::make_unique<StubTTSEngine>();
  }

  TTSConfig tts_cfg;
  tts_cfg.engine             = engine_name;
  tts_cfg.mode               = cfg.mode;
  tts_cfg.model_path         = cfg.model_path;
  tts_cfg.piper_data_path    = cfg.piper_data_path;
  tts_cfg.speaker_id         = cfg.speaker_id;
  tts_cfg.gpu_enabled        = cfg.enable_gpu;
  tts_cfg.output_sample_rate = cfg.output_sample_rate;
  tts_cfg.preview_engine     = cfg.preview_engine;
  tts_cfg.max_primary_tts_budget_ms = cfg.max_primary_tts_budget_ms;

  if (!engine->initialize(tts_cfg, error)) {
    return nullptr;
  }
  return engine;
}

// Convert float PCM samples to int16 for VAD processing.
// Clamps to [-1, 1] range before scaling.
inline int16_t float_to_int16(float v) {
  v = (v < -1.0F) ? -1.0F : (v > 1.0F) ? 1.0F : v;
  return static_cast<int16_t>(v * 32767.0F);
}

bool initialize_audio_backends(const AppConfig& config,
                               std::unique_ptr<IAudioInput>& audio_input,
                               std::unique_ptr<IAudioOutput>& audio_output) {
  if (config.runtime.use_simulated_audio) {
    audio_input  = std::make_unique<SimulatedAudioInput>(
        config.audio.sample_rate_hz, config.audio.input_channels,
        config.audio.frames_per_buffer);
    audio_output = std::make_unique<SimulatedAudioOutput>(
        config.audio.sample_rate_hz, config.audio.output_channels,
        config.audio.frames_per_buffer);
    return true;
  }

#if defined(MEV_ENABLE_PORTAUDIO)
  audio_input  = std::make_unique<PortAudioInput>(
      config.audio.sample_rate_hz, config.audio.input_channels,
      config.audio.frames_per_buffer, config.audio.input_device);
  audio_output = std::make_unique<PortAudioOutput>(
      config.audio.sample_rate_hz, config.audio.output_channels,
      config.audio.frames_per_buffer, config.audio.output_device);
  return true;
#else
  MEV_LOG_ERROR("runtime.use_simulated_audio=false requires a build with "
                "MEV_ENABLE_PORTAUDIO=ON");
  return false;
#endif
}

bool initialize_vad_engine(const AppConfig& config, std::unique_ptr<IVadEngine>& vad_engine) {
  if (config.vad.engine == "none") {
    vad_engine = std::make_unique<NullVadEngine>();
    return vad_engine->initialize(config.vad);
  }

  if (config.vad.engine == "webrtcvad") {
#if defined(MEV_ENABLE_WEBRTCVAD)
    vad_engine = std::make_unique<WebRtcVadEngine>();
    return vad_engine->initialize(config.vad);
#else
    MEV_LOG_ERROR("vad.engine=webrtcvad requires a build with "
                  "MEV_ENABLE_WEBRTCVAD=ON");
    return false;
#endif
  }

  MEV_LOG_ERROR("unsupported vad.engine='", config.vad.engine,
                "' (supported: none, webrtcvad)");
  return false;
}

std::unique_ptr<IASREngine> make_asr_engine(const AsrConfig& cfg) {
#if defined(MEV_ENABLE_WHISPER_CPP)
  return std::make_unique<WhisperASREngine>(
      cfg.model_path, cfg.enable_gpu, cfg.language, cfg.translate, cfg.quantization);
#else
  return std::make_unique<WhisperAsrStub>(cfg.model_path, cfg.enable_gpu);
#endif
}

}  // namespace

// ---------------------------------------------------------------------------
PipelineOrchestrator::PipelineOrchestrator(AppConfig config) : config_(std::move(config)) {}
PipelineOrchestrator::~PipelineOrchestrator() { stop(); }

// ---------------------------------------------------------------------------
bool PipelineOrchestrator::start() {
  PipelineState expected = PipelineState::kStopped;
  if (!state_.compare_exchange_strong(expected, PipelineState::kStarting)) return false;

  Logger::instance().set_level(config_.logging.level);

  std::string error;
  if (!validate_config(config_, error)) {
    MEV_LOG_ERROR("invalid config: ", error);
    state_.store(PipelineState::kFailed, std::memory_order_release);
    return false;
  }

  if (!initialize_components()) {
    state_.store(PipelineState::kFailed, std::memory_order_release);
    return false;
  }

  if (!warmup_models()) {
    state_.store(PipelineState::kFailed, std::memory_order_release);
    return false;
  }

  // Start downstream workers first to avoid queue back-pressure before upstream is ready.
  supervisor_thread_ = std::jthread([this](std::stop_token t) { supervisor_loop(t); });
  tts_thread_        = std::jthread([this](std::stop_token t) { tts_loop(t); });
  text_thread_       = std::jthread([this](std::stop_token t) { text_loop(t); });
  asr_thread_        = std::jthread([this](std::stop_token t) { asr_loop(t); });
  ingest_thread_     = std::jthread([this](std::stop_token t) { ingest_loop(t); });

  const bool out_ok = audio_output_->start(
      [this](float* out, std::size_t frames, std::uint16_t ch, std::uint32_t sr) {
        on_audio_output_callback(out, frames, ch, sr);
      });
  const bool in_ok = audio_input_->start(
      [this](const float* in, std::size_t frames, std::uint16_t ch, std::uint32_t sr) {
        on_audio_input_callback(in, frames, ch, sr);
      });

  if (!out_ok || !in_ok) {
    MEV_LOG_ERROR("audio backend failed to start");
    stop();
    state_.store(PipelineState::kFailed, std::memory_order_release);
    return false;
  }

  state_.store(PipelineState::kRunning, std::memory_order_release);
  MEV_LOG_INFO("pipeline running — input=", audio_input_->name(),
               " asr=", asr_engine_->name(),
               " tts=", tts_engine_->engine_name(),
               " mode=NORMAL");
  return true;
}

// ---------------------------------------------------------------------------
void PipelineOrchestrator::stop() {
  const auto prev = state_.load(std::memory_order_acquire);
  if (prev == PipelineState::kStopped || prev == PipelineState::kStopping) return;

  state_.store(PipelineState::kStopping, std::memory_order_release);

  if (audio_input_)  audio_input_->stop();
  if (audio_output_) audio_output_->stop();

  auto stop_thread = [](std::jthread& t) {
    if (t.joinable()) { t.request_stop(); t.join(); }
  };

  stop_thread(ingest_thread_);
  stop_thread(asr_thread_);
  stop_thread(text_thread_);
  stop_thread(tts_thread_);
  stop_thread(supervisor_thread_);

  if (tts_engine_)          tts_engine_->shutdown();
  if (tts_fallback_engine_) tts_fallback_engine_->shutdown();

  state_.store(PipelineState::kStopped, std::memory_order_release);
}

// ---------------------------------------------------------------------------
bool PipelineOrchestrator::initialize_components() {
  input_ring_    = std::make_unique<SpscRingBuffer<RawAudioBlock>>(config_.audio.input_ring_capacity);
  ingest_to_asr_ = std::make_unique<SpscRingBuffer<std::unique_ptr<Utterance>>>(config_.queues.ingest_to_asr_capacity);
  asr_to_text_   = std::make_unique<SpscRingBuffer<std::unique_ptr<Utterance>>>(config_.queues.asr_to_text_capacity);
  text_to_tts_   = std::make_unique<SpscRingBuffer<std::unique_ptr<Utterance>>>(config_.pipeline.max_queue_depth + 2U);
  tts_to_output_ = std::make_unique<SpscRingBuffer<OutputAudioBlock>>(config_.audio.output_ring_capacity);

  if (!initialize_audio_backends(config_, audio_input_, audio_output_)) {
    return false;
  }

  // ASR — whisper.cpp when compiled in, stub otherwise.
#if defined(MEV_ENABLE_WHISPER_CPP)
  asr_engine_ = make_asr_engine(config_.asr);
#else
  asr_engine_ = make_asr_engine(config_.asr);
#endif

  // TTS engine selection.
  std::string tts_error;
  tts_engine_ = make_tts_engine(config_.tts.engine, config_.tts, tts_error);
  if (!tts_engine_) {
    MEV_LOG_ERROR("TTS '", config_.tts.engine, "' failed: ", tts_error,
                  " — trying fallback '", config_.tts.fallback_engine, "'");
    tts_engine_ = make_tts_engine(config_.tts.fallback_engine, config_.tts, tts_error);
    if (!tts_engine_) {
      MEV_LOG_ERROR("TTS fallback also failed: ", tts_error);
      return false;
    }
    transition_to_mode(PipelineMode::DEGRADED);
  }

  // Keep the preview/fallback engine ready for low-latency clauses and failures.
  {
    std::string fb_err;
    tts_fallback_engine_ = make_tts_engine(config_.tts.preview_engine, config_.tts, fb_err);
    // Non-fatal: preview engine may not be installed in minimal builds.
  }

  // Domain adapter.
  domain_context_ = std::make_shared<DomainContextManager>(config_.domain);
  domain_adapter_ = std::make_unique<TechnicalDomainAdapter>(domain_context_);
  std::string domain_err;
  if (!domain_adapter_->initialize(config_.domain, domain_err)) {
    MEV_LOG_WARN("domain adapter init: ", domain_err);
  }

  // TTS scheduler.
  tts_scheduler_ = std::make_unique<TtsScheduler>(TtsSchedulerPolicy{
      .max_queue_depth     = config_.pipeline.max_queue_depth,
      .backlog_soft_limit  = static_cast<std::size_t>(config_.pipeline.max_queue_depth / 2U),
      .partial_backlog_limit = 1U,
      .output_backlog_limit  = config_.pipeline.max_queue_depth + 1U,
      .stale_after_ms      = config_.pipeline.stale_after_ms,
      .stale_after_n_newer = config_.pipeline.stale_after_n_newer,
      .chunk_deadline_slack_ms = 30U,
      .drop_policy         = config_.pipeline.drop_policy,
  });

  if (!initialize_vad_engine(config_, vad_engine_)) {
    MEV_LOG_ERROR("failed to initialize VAD backend for vad.engine='",
                  config_.vad.engine, "'");
    return false;
  }

  // Resamplers.
#if defined(MEV_ENABLE_LIBSAMPLERATE)
  ingest_resampler_ = std::make_unique<Resampler>();
  ingest_resampler_->initialize(
      16000.0 / static_cast<double>(config_.audio.sample_rate_hz), 1);
#endif

  if (!configure_tts_resampler_for_active_engine()) {
    return false;
  }

  return true;
}

// ---------------------------------------------------------------------------
bool PipelineOrchestrator::warmup_models() {
  const auto t0 = std::chrono::steady_clock::now();

  std::string error;
  if (!asr_engine_->warmup(error)) {
    MEV_LOG_ERROR("ASR warmup failed: ", error);

    if (config_.asr.enable_gpu &&
        config_.resilience.gpu_failure_action == "fallback_cpu") {
      MEV_LOG_WARN("retrying ASR warmup on CPU after GPU warmup failure");
      metrics_.inc_degradation_event();

      auto cpu_cfg = config_.asr;
      cpu_cfg.enable_gpu = false;
      if (cpu_cfg.quantization == "f16") {
        cpu_cfg.quantization = "q5_1";
      }

      asr_engine_ = make_asr_engine(cpu_cfg);
      std::string cpu_error;
      if (asr_engine_->warmup(cpu_error)) {
        config_.asr = cpu_cfg;
        transition_to_mode(PipelineMode::DEGRADED);
        MEV_LOG_WARN("ASR running in CPU fallback mode "
                     "(gpu=false quantization=", config_.asr.quantization, ")");
      } else {
        MEV_LOG_ERROR("ASR CPU fallback warmup failed: ", cpu_error);
        if (!config_.resilience.enable_degradation) return false;
        MEV_LOG_WARN("degrading to MINIMAL mode after ASR warmup failure");
        metrics_.inc_degradation_event();
        transition_to_mode(PipelineMode::MINIMAL);
      }
    } else {
      if (!config_.resilience.enable_degradation) return false;
      MEV_LOG_WARN("degrading to MINIMAL mode after ASR warmup failure");
      metrics_.inc_degradation_event();
      transition_to_mode(PipelineMode::MINIMAL);
    }
  }

  tts_engine_->warmup();
  std::vector<float> tts_warmup_pcm;
  if (!tts_engine_->synthesize("warmup", tts_warmup_pcm) || tts_warmup_pcm.empty()) {
    MEV_LOG_ERROR("TTS warmup synthesis failed for engine='", tts_engine_->engine_name(), "'");
    if (!activate_tts_fallback("warmup failure")) {
      if (!config_.resilience.enable_degradation) return false;
      MEV_LOG_WARN("degrading to MINIMAL mode after TTS warmup failure");
      metrics_.inc_degradation_event();
      transition_to_mode(PipelineMode::MINIMAL);
    }
  }

  if (tts_fallback_engine_ &&
      tts_fallback_engine_->engine_name() != tts_engine_->engine_name()) {
    tts_fallback_engine_->warmup();
  }

  const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now() - t0).count();
  MEV_LOG_INFO("warmup completed in ", ms, "ms — pipeline ready");

  set_ready(true);
  return true;
}

// ---------------------------------------------------------------------------
bool PipelineOrchestrator::configure_tts_resampler_for_active_engine() {
#if defined(MEV_ENABLE_LIBSAMPLERATE)
  tts_resampler_ = std::make_unique<Resampler>();
  const int source_rate = (tts_engine_ != nullptr)
                              ? std::max(tts_engine_->output_sample_rate(), 1)
                              : static_cast<int>(config_.tts.output_sample_rate);
  return tts_resampler_->initialize(
      static_cast<double>(config_.audio.sample_rate_hz) /
          static_cast<double>(source_rate),
      1);
#else
  return true;
#endif
}

// ---------------------------------------------------------------------------
bool PipelineOrchestrator::activate_tts_fallback(const char* reason) {
  if (!tts_fallback_engine_) {
    MEV_LOG_ERROR("TTS fallback unavailable after ", reason);
    return false;
  }

  if (tts_engine_ && tts_engine_->engine_name() == tts_fallback_engine_->engine_name()) {
    return false;
  }

  MEV_LOG_WARN("switching TTS engine from '",
               (tts_engine_ ? tts_engine_->engine_name() : std::string("unknown")),
               "' to fallback '", tts_fallback_engine_->engine_name(),
               "' after ", reason);
  tts_engine_ = std::move(tts_fallback_engine_);
  metrics_.inc_degradation_event();
  transition_to_mode(PipelineMode::DEGRADED);
  return configure_tts_resampler_for_active_engine();
}

// ---------------------------------------------------------------------------
void PipelineOrchestrator::transition_to_mode(const PipelineMode mode) {
  const auto prev = mode_.exchange(mode, std::memory_order_acq_rel);
  if (prev == mode) return;
  switch (mode) {
    case PipelineMode::DEGRADED:    MEV_LOG_WARN("[DEGRADATION] mode=DEGRADED"); break;
    case PipelineMode::MINIMAL:     MEV_LOG_ERROR("[DEGRADATION] mode=MINIMAL"); break;
    case PipelineMode::PASSTHROUGH: MEV_LOG_ERROR("[DEGRADATION] mode=PASSTHROUGH"); break;
    case PipelineMode::NORMAL:      MEV_LOG_INFO("[RECOVERY] mode=NORMAL"); break;
  }
}

// ---------------------------------------------------------------------------
bool PipelineOrchestrator::try_activate_tts_on_gpu() {
  if (!config_.gpu.enabled || !config_.gpu.asr_priority) return true;
  if (gpu_scheduler_.tts_try_acquire()) return true;
  if (config_.gpu.tts_cpu_fallback_on_contention) {
    metrics_.inc_gpu_contention_fallback();
    MEV_LOG_DEBUG("TTS falling back to CPU (ASR holds GPU)");
    return false;
  }
  std::this_thread::sleep_for(std::chrono::milliseconds(5));
  return gpu_scheduler_.tts_try_acquire();
}

// ---------------------------------------------------------------------------
// RT callbacks — ZERO allocations, ZERO blocking
// ---------------------------------------------------------------------------
void PipelineOrchestrator::on_audio_input_callback(const float* input, const std::size_t frames,
                                                    const std::uint16_t channels,
                                                    const std::uint32_t sample_rate) {
  if (frames > kMaxFramesPerBlock || channels == 0 || channels > kMaxAudioChannels) {
    metrics_.inc_input_overrun();
    return;
  }
  RawAudioBlock block;
  block.sequence    = input_sequence_.fetch_add(1, std::memory_order_relaxed);
  block.capture_time = Clock::now();
  block.frames      = static_cast<std::uint16_t>(frames);
  block.channels    = channels;
  block.sample_rate = sample_rate;
  std::memcpy(block.interleaved.data(), input, frames * channels * sizeof(float));

  if (!input_ring_->try_push(std::move(block))) {
    metrics_.inc_input_overrun();
  }
}

void PipelineOrchestrator::on_audio_output_callback(float* output, const std::size_t frames,
                                                     const std::uint16_t channels,
                                                     const std::uint32_t /*sample_rate*/) {
  if (mode_.load(std::memory_order_relaxed) == PipelineMode::PASSTHROUGH) {
    std::fill(output, output + frames * channels, 0.0F);
    return;
  }

  for (std::size_t frame = 0; frame < frames; ++frame) {
    if (!has_current_output_block_ || current_output_offset_ >= current_output_block_.frames) {
      OutputAudioBlock next;
      if (!tts_to_output_->try_pop(next)) {
        for (std::uint16_t ch = 0; ch < channels; ++ch) {
          output[frame * channels + ch] = 0.0F;
        }
        metrics_.inc_output_underrun();
        continue;
      }
      current_output_block_  = std::move(next);
      current_output_offset_ = 0;
      has_current_output_block_ = true;
    }
    const float s = current_output_block_.mono[current_output_offset_++];
    for (std::uint16_t ch = 0; ch < channels; ++ch) {
      output[frame * channels + ch] = s;
    }
  }
}

// ---------------------------------------------------------------------------
// Thread 2 — Ingest Worker
// ---------------------------------------------------------------------------
void PipelineOrchestrator::ingest_loop(std::stop_token token) {
  // Pre-allocated resampling buffer (max possible 16kHz output for 1 block).
  std::vector<float> resampled_mono;
  resampled_mono.reserve(4096U);

  // Pre-allocated VAD int16 buffer (30ms @ 16kHz = 480 samples).
  std::vector<int16_t> vad_frame_int16;
  vad_frame_int16.reserve(512U);

  // Rolling mono PCM accumulator (may be at mic rate or 16kHz depending on resampler).
  std::vector<float> rolling_pcm;
  rolling_pcm.reserve(static_cast<std::size_t>(config_.audio.sample_rate_hz) * 4U);

  // VAD state machine.
  enum class VadState : std::uint8_t { SILENCE, SPEECH };
  VadState vad_state = VadState::SILENCE;
  std::vector<float> speech_buffer;
  speech_buffer.reserve(static_cast<std::size_t>(config_.vad.max_chunk_duration_ms) *
                        16000U / 1000U + 512U);

  // How many 16kHz samples represent our VAD frame (use 30ms).
  constexpr std::uint32_t kVadFrameMs = 30U;
  const std::size_t vad_frame_samples = (16000U * kVadFrameMs) / 1000U;  // 480 samples @ 16kHz

  // Silence threshold in samples.
  const std::size_t silence_samples =
      static_cast<std::size_t>(config_.vad.silence_duration_ms) * 16000U / 1000U;
  const std::size_t max_chunk_samples =
      static_cast<std::size_t>(config_.vad.max_chunk_duration_ms) * 16000U / 1000U;
  const std::size_t leading_pad_samples =
      static_cast<std::size_t>(config_.vad.leading_pad_ms) * 16000U / 1000U;

  std::size_t silence_frame_count = 0;  // frames of silence seen while in SPEECH state

  // Sliding pre-speech buffer for leading pad.
  std::vector<float> pre_speech_ring;
  pre_speech_ring.reserve(leading_pad_samples + vad_frame_samples);

  // Legacy fixed-window params (fallback when VAD engine is NullVadEngine).
  const auto chunk_samples = (static_cast<std::uint64_t>(config_.asr.chunk_ms) *
                               config_.audio.sample_rate_hz) / 1000ULL;
  const auto hop_samples   = (static_cast<std::uint64_t>(config_.asr.hop_ms) *
                               config_.audio.sample_rate_hz) / 1000ULL;
  std::size_t consumed_offset = 0;

  const bool use_vad = (config_.vad.engine != "none");

  while (!token.stop_requested()) {
    RawAudioBlock block;
    if (!input_ring_->try_pop(block)) {
      std::this_thread::sleep_for(std::chrono::microseconds(kSleepMicros));
      continue;
    }

    MEV_PROFILE_SCOPE("ingest_frame");
    StageTimer timer(metrics_, StageId::kAudioIngest);
    const auto capture_time = block.capture_time;

    // --- Step 1: downmix to mono -------------------------------------------
    for (std::size_t frame = 0; frame < block.frames; ++frame) {
      float mono = 0.0F;
      for (std::size_t ch = 0; ch < block.channels; ++ch) {
        mono += block.interleaved[frame * block.channels + ch];
      }
      rolling_pcm.push_back(mono / static_cast<float>(block.channels));
    }

    // --- Step 2: resample to 16kHz if needed --------------------------------
    const float* pcm_16k    = nullptr;
    std::size_t  pcm_16k_n  = 0;

    if (ingest_resampler_ != nullptr) {
      const std::size_t out_cap =
          static_cast<std::size_t>(
              static_cast<double>(rolling_pcm.size()) * ingest_resampler_->ratio()) + 64U;
      if (resampled_mono.size() < out_cap) resampled_mono.resize(out_cap);
      const std::size_t n = ingest_resampler_->process(
          rolling_pcm.data(), rolling_pcm.size(),
          resampled_mono.data(), resampled_mono.size());
      rolling_pcm.clear();
      pcm_16k   = resampled_mono.data();
      pcm_16k_n = n;
    } else {
      pcm_16k   = rolling_pcm.data();
      pcm_16k_n = rolling_pcm.size();
    }

    if (pcm_16k_n == 0) continue;

    if (!use_vad) {
      // -----------------------------------------------------------------------
      // Legacy fixed-window chunking path (vad.engine = "none").
      // -----------------------------------------------------------------------
      if (ingest_resampler_ == nullptr) {
        // rolling_pcm was not cleared; use consumed_offset approach.
        while (!token.stop_requested() &&
               (rolling_pcm.size() - consumed_offset) >= chunk_samples) {
          if (!pipeline_ready_.load(std::memory_order_acquire)) {
            consumed_offset += hop_samples;
            continue;
          }
          auto utt = std::make_unique<Utterance>();
          utt->id    = next_utterance_id_.fetch_add(1, std::memory_order_relaxed);
          utt->state = UtteranceState::QUEUED_FOR_ASR;
          utt->metrics.capture_start = capture_time;
          utt->metrics.capture_end   = Clock::now();
          utt->asr_prompt_hint       = domain_adapter_->generate_asr_prompt();
          utt->stream_continues      = true;
          utt->source_pcm.assign(
              rolling_pcm.begin() + static_cast<long long>(consumed_offset),
              rolling_pcm.begin() + static_cast<long long>(consumed_offset + chunk_samples));
          if (!ingest_to_asr_->try_push(std::move(utt))) {
            metrics_.inc_queue_drop();
          } else {
            metrics_.inc_asr_requests();
          }
          consumed_offset += hop_samples;
        }
        if (consumed_offset > rolling_pcm.size() / 2U) {
          rolling_pcm.erase(rolling_pcm.begin(),
                            rolling_pcm.begin() + static_cast<long long>(consumed_offset));
          consumed_offset = 0;
        }
      } else {
        // Resampled path: treat pcm_16k as a flat chunk.
        if (!pipeline_ready_.load(std::memory_order_acquire)) {
          // discard
        } else if (pcm_16k_n >= chunk_samples) {
          auto utt = std::make_unique<Utterance>();
          utt->id    = next_utterance_id_.fetch_add(1, std::memory_order_relaxed);
          utt->state = UtteranceState::QUEUED_FOR_ASR;
          utt->metrics.capture_start = capture_time;
          utt->metrics.capture_end   = Clock::now();
          utt->asr_prompt_hint       = domain_adapter_->generate_asr_prompt();
          utt->stream_continues      = true;
          utt->source_pcm.assign(pcm_16k, pcm_16k + chunk_samples);
          if (!ingest_to_asr_->try_push(std::move(utt))) {
            metrics_.inc_queue_drop();
          } else {
            metrics_.inc_asr_requests();
          }
        }
      }
      continue;
    }

    // -----------------------------------------------------------------------
    // VAD-based chunking path.
    // -----------------------------------------------------------------------
    std::size_t offset = 0;
    while (offset + vad_frame_samples <= pcm_16k_n && !token.stop_requested()) {
      // Convert one VAD frame from float to int16.
      vad_frame_int16.resize(vad_frame_samples);
      for (std::size_t i = 0; i < vad_frame_samples; ++i) {
        vad_frame_int16[i] = float_to_int16(pcm_16k[offset + i]);
      }

      const float speech_prob = vad_engine_->process_frame(
          vad_frame_int16.data(), vad_frame_samples);
      const bool is_speech = (speech_prob >= config_.vad.threshold);

      if (vad_state == VadState::SILENCE) {
        // Maintain a rolling pre-speech buffer for leading pad.
        pre_speech_ring.insert(pre_speech_ring.end(),
                               pcm_16k + offset,
                               pcm_16k + offset + vad_frame_samples);
        if (pre_speech_ring.size() > leading_pad_samples + vad_frame_samples) {
          const std::size_t excess = pre_speech_ring.size() -
                                     (leading_pad_samples + vad_frame_samples);
          pre_speech_ring.erase(pre_speech_ring.begin(),
                                pre_speech_ring.begin() +
                                    static_cast<std::ptrdiff_t>(excess));
        }

        if (is_speech) {
          vad_state = VadState::SPEECH;
          silence_frame_count = 0;
          // Seed speech buffer with leading pad.
          speech_buffer.clear();
          speech_buffer.insert(speech_buffer.end(),
                               pre_speech_ring.begin(), pre_speech_ring.end());
          pre_speech_ring.clear();
        }
      } else {
        // SPEECH state: accumulate frame.
        speech_buffer.insert(speech_buffer.end(),
                             pcm_16k + offset,
                             pcm_16k + offset + vad_frame_samples);

        if (!is_speech) {
          ++silence_frame_count;
          const std::size_t silence_so_far = silence_frame_count * vad_frame_samples;
          if (silence_so_far >= silence_samples) {
            // End of utterance by silence timeout.
            if (pipeline_ready_.load(std::memory_order_acquire) &&
                !speech_buffer.empty()) {
              auto utt = std::make_unique<Utterance>();
              utt->id    = next_utterance_id_.fetch_add(1, std::memory_order_relaxed);
              utt->state = UtteranceState::QUEUED_FOR_ASR;
              utt->metrics.capture_start = capture_time;
              utt->metrics.capture_end   = Clock::now();
              utt->asr_prompt_hint       = domain_adapter_->generate_asr_prompt();
              utt->stream_continues      = false;
              utt->source_pcm           = std::move(speech_buffer);
              if (!ingest_to_asr_->try_push(std::move(utt))) {
                metrics_.inc_queue_drop();
              } else {
                metrics_.inc_asr_requests();
              }
            }
            speech_buffer.clear();
            speech_buffer.reserve(max_chunk_samples + 512U);
            vad_state = VadState::SILENCE;
            silence_frame_count = 0;
          }
        } else {
          // Back to active speech — reset silence counter.
          silence_frame_count = 0;
        }

        // Force-close chunk if it exceeds max_chunk_duration_ms.
        if (speech_buffer.size() >= max_chunk_samples) {
          if (pipeline_ready_.load(std::memory_order_acquire)) {
            auto utt = std::make_unique<Utterance>();
            utt->id    = next_utterance_id_.fetch_add(1, std::memory_order_relaxed);
            utt->state = UtteranceState::QUEUED_FOR_ASR;
            utt->metrics.capture_start = capture_time;
            utt->metrics.capture_end   = Clock::now();
            utt->asr_prompt_hint       = domain_adapter_->generate_asr_prompt();
            utt->stream_continues      = true;
            utt->source_pcm           = std::move(speech_buffer);
            if (!ingest_to_asr_->try_push(std::move(utt))) {
              metrics_.inc_queue_drop();
            } else {
              metrics_.inc_asr_requests();
            }
          }
          speech_buffer.clear();
          speech_buffer.reserve(max_chunk_samples + 512U);
          vad_state = VadState::SILENCE;
          silence_frame_count = 0;
        }
      }

      offset += vad_frame_samples;
    }
  }
}

// ---------------------------------------------------------------------------
// Thread 3 — ASR Worker
// ---------------------------------------------------------------------------
void PipelineOrchestrator::asr_loop(std::stop_token token) {
  while (!token.stop_requested()) {
    std::unique_ptr<Utterance> utt;
    if (!ingest_to_asr_->try_pop(utt)) {
      std::this_thread::sleep_for(std::chrono::microseconds(kSleepMicros));
      continue;
    }
    if (!utt) continue;

    utt->state = UtteranceState::TRANSCRIBING;
    utt->metrics.asr_start = Clock::now();

    {
      AsrGpuGuard gpu_guard(gpu_scheduler_);  // ASR holds GPU for duration of inference
      MEV_PROFILE_SCOPE("asr_inference");
      StageTimer timer(metrics_, StageId::kAsr);

      // Update domain prompt before inference.
      if (config_.asr.use_domain_prompt) {
        const auto prompt = domain_adapter_->generate_asr_prompt();
#if defined(MEV_ENABLE_WHISPER_CPP)
        if (auto* we = dynamic_cast<WhisperASREngine*>(asr_engine_.get())) {
          we->set_domain_prompt(prompt);
        }
#else
        (void)prompt;
#endif
      }

      AsrRequest req;
      req.sequence    = static_cast<SequenceNumber>(utt->id);
      req.created_at  = utt->metrics.capture_start;
      req.sample_rate = 16000;  // ingest loop resamples to 16kHz
      req.prompt_hint = utt->asr_prompt_hint;
      req.stream_continues = utt->stream_continues;
      req.mono_pcm    = utt->source_pcm;

      auto partial = asr_engine_->transcribe_incremental(req);
      utt->source_text          = partial.source_text_es;
      utt->translated_text      = partial.translated_text_en;
      utt->raw_translated_text  = partial.raw_translated_text_en;
      utt->stable_prefix_text   = partial.stable_prefix_en;
      utt->asr_stability        = partial.stability;
      utt->asr_revision         = partial.revision;
      utt->asr_is_partial       = partial.is_partial;
    }

    utt->metrics.asr_end = Clock::now();
    utt->state = UtteranceState::COMMITTED;

    if (!asr_to_text_->try_push(std::move(utt))) {
      metrics_.inc_queue_drop();
    }
  }
}

// ---------------------------------------------------------------------------
// Thread 4 — Text Processing Worker
// ---------------------------------------------------------------------------
void PipelineOrchestrator::text_loop(std::stop_token token) {
  while (!token.stop_requested()) {
    std::unique_ptr<Utterance> utt;
    if (!asr_to_text_->try_pop(utt)) {
      std::this_thread::sleep_for(std::chrono::microseconds(kSleepMicros));
      continue;
    }
    if (!utt) continue;

    utt->state = UtteranceState::NORMALIZING;

    {
      StageTimer norm_timer(metrics_, StageId::kNormalize);
      MEV_PROFILE_SCOPE("text_processing");

      const auto corrected = domain_adapter_->correct_asr_output(utt->translated_text);
      domain_adapter_->update_session_context(corrected);
      utt->normalized_text = domain_adapter_->prepare_for_tts(corrected);
    }

    utt->metrics.normalize_end = Clock::now();

    if (utt->normalized_text.empty()) {
      utt->state = UtteranceState::DROPPED;
      continue;
    }

    const auto queue_depth = text_to_tts_->size_approx();
    auto scheduled = tts_scheduler_->schedule(std::move(utt), queue_depth);
    if (!scheduled) {
      metrics_.inc_queue_drop();
      continue;
    }

    if (!text_to_tts_->try_push(std::move(scheduled))) {
      metrics_.inc_queue_drop();
    }
  }
}

// ---------------------------------------------------------------------------
// Thread 5 — TTS Worker
// ---------------------------------------------------------------------------
void PipelineOrchestrator::tts_loop(std::stop_token token) {
  while (!token.stop_requested()) {
    std::unique_ptr<Utterance> utt;
    if (!text_to_tts_->try_pop(utt)) {
      std::this_thread::sleep_for(std::chrono::microseconds(kSleepMicros));
      continue;
    }
    if (!utt) continue;

    if (tts_scheduler_->should_cancel_as_stale(*utt, Clock::now())) {
      metrics_.inc_stale_cancelled();
      utt->state = UtteranceState::DROPPED;
      MEV_LOG_DEBUG("[DROPPED] id=", utt->id, " reason=stale");
      continue;
    }

    utt->state = UtteranceState::SYNTHESIZING;
    utt->metrics.tts_start = Clock::now();

    const bool start_with_preview =
        (config_.tts.mode == "interactive_preview") || prefer_tts_preview_;
    const bool use_gpu = (!start_with_preview && tts_engine_ != nullptr &&
                          tts_engine_->using_gpu())
                             ? try_activate_tts_on_gpu()
                             : false;

    {
      MEV_PROFILE_SCOPE("tts_inference");
      StageTimer tts_timer(metrics_, StageId::kTts);
      metrics_.inc_tts_requests();

      utt->speech_chunks = chunk_text_for_realtime_tts(
          static_cast<SequenceNumber>(utt->id), utt->normalized_text, utt->asr_is_partial,
          Clock::now(), config_.tts.max_primary_tts_budget_ms);
      utt->speech_chunks = tts_scheduler_->select_chunks_for_synthesis(
          *utt, Clock::now(), tts_to_output_->size_approx());

      bool use_preview_for_remaining = start_with_preview;
      utt->tts_used_preview_engine = use_preview_for_remaining;
      utt->synth_pcm.clear();

      auto enqueue_chunk_pcm = [&](const std::vector<float>& input_pcm,
                                   const int source_rate) -> bool {
        std::vector<float>* output_pcm = const_cast<std::vector<float>*>(&input_pcm);
        std::vector<float> resampled_pcm;
        if (tts_resampler_ != nullptr && source_rate == tts_engine_->output_sample_rate()) {
          const std::size_t out_capacity =
              static_cast<std::size_t>(
                  static_cast<double>(input_pcm.size()) * tts_resampler_->ratio()) + 64U;
          resampled_pcm.resize(out_capacity);
          const std::size_t n = tts_resampler_->process(
              input_pcm.data(), input_pcm.size(),
              resampled_pcm.data(), resampled_pcm.size());
          resampled_pcm.resize(n);
          output_pcm = &resampled_pcm;
        } else if (source_rate != static_cast<int>(config_.audio.sample_rate_hz)) {
#if defined(MEV_ENABLE_LIBSAMPLERATE)
          Resampler local_resampler;
          if (!local_resampler.initialize(
                  static_cast<double>(config_.audio.sample_rate_hz) /
                      static_cast<double>(std::max(source_rate, 1)),
                  1)) {
            return false;
          }
          const std::size_t out_capacity =
              static_cast<std::size_t>(
                  static_cast<double>(input_pcm.size()) * local_resampler.ratio()) + 64U;
          resampled_pcm.resize(out_capacity);
          const std::size_t n = local_resampler.process(
              input_pcm.data(), input_pcm.size(),
              resampled_pcm.data(), resampled_pcm.size());
          resampled_pcm.resize(n);
          output_pcm = &resampled_pcm;
#endif
        }

        if (utt->metrics.output_start == TimePoint{}) {
          utt->metrics.output_start = Clock::now();
        }

        std::size_t offset = 0;
        while (offset < output_pcm->size()) {
          OutputAudioBlock blk;
          blk.sequence = static_cast<SequenceNumber>(utt->id);
          const auto remaining = output_pcm->size() - offset;
          blk.frames = static_cast<std::uint16_t>(
              std::min<std::size_t>(remaining, config_.audio.frames_per_buffer));
          std::copy_n(output_pcm->begin() + static_cast<long long>(offset),
                      blk.frames, blk.mono.begin());
          offset += blk.frames;
          if (!tts_to_output_->try_push(std::move(blk))) {
            metrics_.inc_queue_drop();
            return false;
          }
        }
        return true;
      };

      bool all_chunks_ok = !utt->speech_chunks.empty();
      if (utt->asr_is_partial && utt->speech_chunks.empty()) {
        metrics_.inc_stale_cancelled();
        utt->state = UtteranceState::DROPPED;
        if (use_gpu) gpu_scheduler_.tts_release();
        continue;
      }
      for (auto& chunk : utt->speech_chunks) {
        ITTSEngine* active_engine =
            (use_preview_for_remaining && tts_fallback_engine_)
                ? tts_fallback_engine_.get()
                : tts_engine_.get();
        if (!active_engine) {
          all_chunks_ok = false;
          break;
        }

        const bool using_primary_engine = (active_engine == tts_engine_.get());
        std::vector<float> chunk_pcm;
        const auto chunk_t0 = std::chrono::steady_clock::now();
        bool ok = active_engine->synthesize_chunk(chunk, chunk_pcm);
        const auto chunk_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - chunk_t0).count();

        if ((!ok || chunk_pcm.empty()) && using_primary_engine && tts_fallback_engine_) {
          MEV_LOG_WARN("primary TTS failed for chunk; using preview engine for id=", utt->id);
          chunk_pcm.clear();
          ok = tts_fallback_engine_->synthesize_chunk(chunk, chunk_pcm);
          use_preview_for_remaining = true;
          prefer_tts_preview_ = ok;
          utt->tts_used_preview_engine = ok;
          if (ok) {
            chunk.sample_rate = static_cast<std::uint32_t>(tts_fallback_engine_->output_sample_rate());
          }
        } else {
          chunk.sample_rate = static_cast<std::uint32_t>(active_engine->output_sample_rate());
        }

        if (!ok || chunk_pcm.empty()) {
          all_chunks_ok = false;
          break;
        }

        chunk.mono_pcm = chunk_pcm;
        utt->synth_pcm.insert(utt->synth_pcm.end(), chunk_pcm.begin(), chunk_pcm.end());

        if (!enqueue_chunk_pcm(chunk_pcm, static_cast<int>(chunk.sample_rate))) {
          all_chunks_ok = false;
          break;
        }

        if (using_primary_engine &&
            config_.tts.mode == "interactive_balanced" &&
            chunk_ms > static_cast<long long>(config_.tts.max_primary_tts_budget_ms) &&
            tts_fallback_engine_) {
          MEV_LOG_WARN("primary TTS exceeded budget (", chunk_ms,
                       "ms > ", config_.tts.max_primary_tts_budget_ms,
                       "ms); switching to preview engine for newer chunks");
          use_preview_for_remaining = true;
          prefer_tts_preview_ = true;
          utt->tts_used_preview_engine = true;
        }
      }

      if (!all_chunks_ok || utt->synth_pcm.empty()) {
        utt->state = UtteranceState::FAILED;
        if (use_gpu) gpu_scheduler_.tts_release();
        continue;
      }
    }

    if (use_gpu) gpu_scheduler_.tts_release();

    utt->metrics.tts_end      = Clock::now();
    utt->state = UtteranceState::QUEUED_FOR_OUTPUT;

    utt->state = UtteranceState::COMPLETED;
    const auto e2e_us = static_cast<std::uint64_t>(
        to_micros(Clock::now() - utt->metrics.capture_start));
    metrics_.record_stage_latency(StageId::kEndToEnd, e2e_us);

    if (config_.telemetry.log_per_utterance) {
      MEV_LOG_INFO("[METRICS] utterance_id=", utt->id,
                   " total_ms=", utt->metrics.total_ms(),
                   " asr_ms=", utt->metrics.asr_ms(),
                   " tts_ms=", utt->metrics.tts_ms(),
                   " speech_chunks=", utt->speech_chunks.size(),
                   " preview=", utt->tts_used_preview_engine,
                   " state=COMPLETED");
    }
  }
}

// ---------------------------------------------------------------------------
// Thread 6 — Supervisor (watchdog + health reporting)
// ---------------------------------------------------------------------------
void PipelineOrchestrator::supervisor_loop(std::stop_token token) {
  using Ms = std::chrono::milliseconds;
  const auto report_interval   = Ms(static_cast<int>(config_.telemetry.report_interval_ms));
  const auto watchdog_interval = Ms(500);
  auto next_report   = std::chrono::steady_clock::now() + report_interval;
  auto next_watchdog = std::chrono::steady_clock::now() + watchdog_interval;

  while (!token.stop_requested()) {
    std::this_thread::sleep_for(Ms(100));
    const auto now = std::chrono::steady_clock::now();

    // ---- Latency watchdog -----------------------------------------------
    if (now >= next_watchdog) {
      next_watchdog = now + watchdog_interval;
      const auto total_q = ingest_to_asr_->size_approx() + text_to_tts_->size_approx();
      const auto est_ms  = static_cast<std::uint32_t>(total_q * config_.asr.chunk_ms);

      if (est_ms > config_.pipeline.critical_threshold_ms) {
        MEV_LOG_ERROR("[WATCHDOG] critical latency est=", est_ms, "ms — backlog too deep");
        metrics_.inc_degradation_event();
      } else if (est_ms > config_.pipeline.warning_threshold_ms) {
        MEV_LOG_WARN("[WATCHDOG] latency warning est=", est_ms, "ms");
      }
    }

    // ---- Periodic health report -----------------------------------------
    if (now >= next_report) {
      next_report = now + report_interval;
      if (state() != PipelineState::kRunning) continue;

      const auto snap = metrics_.snapshot();
      const char* mode_str = [&]() -> const char* {
        switch (mode_.load(std::memory_order_relaxed)) {
          case PipelineMode::NORMAL:      return "NORMAL";
          case PipelineMode::DEGRADED:    return "DEGRADED";
          case PipelineMode::MINIMAL:     return "MINIMAL";
          case PipelineMode::PASSTHROUGH: return "PASSTHROUGH";
        }
        return "?";
      }();

      MEV_LOG_INFO("[HEALTH] mode=", mode_str,
                   " asr_q=", ingest_to_asr_->size_approx(),
                   " tts_q=", text_to_tts_->size_approx(),
                   " overruns=", snap.input_overruns,
                   " underruns=", snap.output_underruns,
                   " drops=", snap.queue_drops,
                   " stale=", snap.stale_cancelled,
                   " asr=", snap.asr_requests,
                   " tts=", snap.tts_requests,
                   " gpu_fallbacks=", snap.gpu_contention_fallbacks,
                   " degradations=", snap.degradation_events);
    }
  }
}

}  // namespace mev
