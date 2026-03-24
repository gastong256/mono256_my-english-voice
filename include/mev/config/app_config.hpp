#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "mev/core/logger.hpp"
#include "mev/pipeline/tts_scheduler.hpp"

namespace mev {

// ---------------------------------------------------------------------------
// AudioConfig — maps to [audio] in pipeline.toml
// ---------------------------------------------------------------------------
struct AudioConfig {
  std::string input_device{"default"};
  std::string output_device{"virtual_mic"};
  std::uint32_t sample_rate_hz{48000};
  std::uint32_t frames_per_buffer{480};
  std::uint16_t input_channels{1};
  std::uint16_t output_channels{1};
  int input_device_id{-1};
  int output_device_id{-1};
  std::size_t input_ring_capacity{16384};
  std::size_t output_ring_capacity{2048};
};

// ---------------------------------------------------------------------------
// VadConfig — maps to [vad] in pipeline.toml
// ---------------------------------------------------------------------------
struct VadConfig {
  std::string engine{"none"};            // "none", "webrtcvad"
  float threshold{0.5F};
  std::uint32_t silence_duration_ms{300};
  std::uint32_t max_chunk_duration_ms{3000};
  std::uint32_t leading_pad_ms{200};
  std::uint32_t trailing_pad_ms{300};
  // TODO(v2): add a real Silero backend before exposing it in public config.
};

// ---------------------------------------------------------------------------
// AsrConfig — maps to [asr] in pipeline.toml
// ---------------------------------------------------------------------------
struct AsrConfig {
  std::string engine{"whisper"};
  std::string model_path{"models/ggml-small.bin"};
  std::string language{"es"};
  bool translate{true};                  // whisper translate mode: es → en
  std::uint32_t beam_size{1};            // 1 = greedy (lowest latency)
  bool enable_gpu{true};
  std::string quantization{"f16"};       // "f16", "q5_1", "q8_0"
  bool use_domain_prompt{true};
  std::uint32_t max_context_tokens{224};
  // TODO(next): partial hypothesis support — interface stub ready
  // Legacy chunking params (kept for compatibility; overridden by VAD when enabled)
  std::uint32_t chunk_ms{640};
  std::uint32_t hop_ms{240};
  float stability_threshold{0.72F};
  std::uint32_t force_commit_ms{450};
};

// ---------------------------------------------------------------------------
// TtsConfig — maps to [tts] in pipeline.toml
// ---------------------------------------------------------------------------
struct TtsConfig {
  std::string engine{"stub"};                         // "piper", "espeak", "stub"
  std::string model_path{"models/en_US-lessac-medium.onnx"};
  std::string piper_data_path{"models/en_US-lessac-medium.onnx.json"};
  std::uint32_t speaker_id{0};
  bool enable_gpu{true};
  std::uint32_t output_sample_rate{22050};
  std::string fallback_engine{"espeak"};
  bool pronunciation_hints_enabled{true};
  std::string hints_path{"config/pronunciation_hints.toml"};
  // Legacy compat fields
  std::string speaker_reference{"configs/voice_reference.wav"};
};

// ---------------------------------------------------------------------------
// QueueConfig — maps to [queues] in pipeline.toml (internal pipeline sizing)
// ---------------------------------------------------------------------------
struct QueueConfig {
  std::size_t ingest_to_asr_capacity{128};
  std::size_t asr_to_text_capacity{128};
  std::size_t text_to_tts_capacity{128};
  // Legacy alias for asr_to_commit (keeps old INI files working)
  std::size_t asr_to_commit_capacity{128};
  std::size_t commit_to_tts_capacity{128};
};

// ---------------------------------------------------------------------------
// PipelineConfig — maps to [pipeline] in pipeline.toml
// Controls backlog management and latency watchdog thresholds.
// ---------------------------------------------------------------------------
struct PipelineConfig {
  std::size_t max_queue_depth{8};
  DropPolicy drop_policy{DropPolicy::kDropOldest};  // "drop_oldest", "coalesce", "none"
  std::uint32_t max_latency_ms{3000};
  std::uint32_t warning_threshold_ms{2000};
  std::uint32_t critical_threshold_ms{4000};
  std::uint32_t stale_after_n_newer{3};
  std::uint32_t stale_after_ms{3000};
};

// ---------------------------------------------------------------------------
// GpuConfig — maps to [gpu] in pipeline.toml
// ---------------------------------------------------------------------------
struct GpuConfig {
  bool enabled{true};
  int device_id{0};
  bool asr_priority{true};
  bool tts_cpu_fallback_on_contention{true};
};

// ---------------------------------------------------------------------------
// DomainConfig — maps to [domain] in pipeline.toml
// ---------------------------------------------------------------------------
struct DomainConfig {
  std::string glossary_path{"config/tech_glossary.toml"};
  bool session_terms_enabled{true};
  bool pronunciation_hints{true};
  std::string initial_prompt_template{"Technical discussion about {topics}"};
  std::vector<std::string> technical_glossary{
      "FastAPI", "PostgreSQL", "Redis", "Kubernetes", "Terraform", "CI/CD", "Docker",
      "observability", "tracing", "rate limiting", "idempotency", "event-driven", "async",
      "throughput", "latency", "SLO", "SLA", "pipeline", "deployment", "production-ready"};
  std::vector<std::string> frequent_phrases{"let's review the architecture", "production incident",
                                            "database migration", "latency budget"};
  std::size_t session_terms_limit{64};
};

// ---------------------------------------------------------------------------
// ResilienceConfig — maps to [resilience] in pipeline.toml
// ---------------------------------------------------------------------------
struct ResilienceConfig {
  bool enable_degradation{true};
  std::string gpu_failure_action{"fallback_cpu"};  // "fallback_cpu", "shutdown"
  bool passthrough_on_total_failure{true};
};

// ---------------------------------------------------------------------------
// LoggingConfig — maps to [logging] in pipeline.toml
// ---------------------------------------------------------------------------
struct LoggingConfig {
  LogLevel level{LogLevel::kInfo};
  std::string file{"logs/pipeline.log"};
  bool console{true};
  bool async{true};
};

// ---------------------------------------------------------------------------
// TelemetryConfig — maps to [telemetry] in pipeline.toml
// ---------------------------------------------------------------------------
struct TelemetryConfig {
  bool enabled{true};
  std::uint32_t report_interval_ms{5000};
  bool log_per_utterance{true};
};

// ---------------------------------------------------------------------------
// RuntimeConfig — internal runtime flags (not in TOML)
// ---------------------------------------------------------------------------
struct RuntimeConfig {
  bool use_simulated_audio{true};
  std::uint32_t run_duration_seconds{20};
};

// ---------------------------------------------------------------------------
// AppConfig — top-level configuration aggregating all subsections.
// ---------------------------------------------------------------------------
struct AppConfig {
  AudioConfig audio{};
  VadConfig vad{};
  AsrConfig asr{};
  TtsConfig tts{};
  QueueConfig queues{};
  PipelineConfig pipeline{};
  GpuConfig gpu{};
  DomainConfig domain{};
  ResilienceConfig resilience{};
  LoggingConfig logging{};
  TelemetryConfig telemetry{};
  RuntimeConfig runtime{};
};

[[nodiscard]] AppConfig default_config();

[[nodiscard]] bool load_config_from_file(const std::string& path, AppConfig& config,
                                         std::string& error);

[[nodiscard]] bool validate_config(const AppConfig& config, std::string& error);

}  // namespace mev
