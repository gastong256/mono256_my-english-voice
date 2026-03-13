#include "mev/config/app_config.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>
#include <unordered_map>

namespace mev {
namespace {

std::string trim(const std::string& input) {
  auto begin = input.begin();
  while (begin != input.end() && std::isspace(static_cast<unsigned char>(*begin)) != 0) {
    ++begin;
  }
  auto end = input.end();
  while (end != begin && std::isspace(static_cast<unsigned char>(*(end - 1))) != 0) {
    --end;
  }
  return std::string(begin, end);
}

bool parse_bool(const std::string& value, bool& out) {
  if (value == "true" || value == "1" || value == "yes" || value == "on") {
    out = true;
    return true;
  }
  if (value == "false" || value == "0" || value == "no" || value == "off") {
    out = false;
    return true;
  }
  return false;
}

template <typename T>
bool parse_number(const std::string& value, T& out) {
  std::istringstream iss(value);
  T parsed{};
  iss >> parsed;
  if (iss.fail()) {
    return false;
  }
  out = parsed;
  return true;
}

std::vector<std::string> parse_csv_list(const std::string& value) {
  std::vector<std::string> out;
  std::stringstream ss(value);
  std::string item;
  while (std::getline(ss, item, ',')) {
    const auto token = trim(item);
    if (!token.empty()) {
      out.push_back(token);
    }
  }
  return out;
}

LogLevel parse_log_level(const std::string& value) {
  if (value == "error") return LogLevel::kError;
  if (value == "warn")  return LogLevel::kWarn;
  if (value == "debug") return LogLevel::kDebug;
  return LogLevel::kInfo;
}

DropPolicy parse_drop_policy(const std::string& value) {
  if (value == "coalesce") return DropPolicy::kCoalesce;
  if (value == "none")     return DropPolicy::kNone;
  return DropPolicy::kDropOldest;  // default
}

}  // namespace

AppConfig default_config() { return AppConfig{}; }

bool load_config_from_file(const std::string& path, AppConfig& config, std::string& error) {
  std::ifstream file(path);
  if (!file.good()) {
    error = "cannot open config: " + path;
    return false;
  }

  std::string section;
  std::string line;
  std::size_t line_num = 0;

  while (std::getline(file, line)) {
    ++line_num;
    const auto clean = trim(line);
    if (clean.empty() || clean[0] == '#' || clean[0] == ';') continue;

    if (clean.front() == '[' && clean.back() == ']') {
      section = trim(clean.substr(1, clean.size() - 2));
      continue;
    }

    const auto eq_pos = clean.find('=');
    if (eq_pos == std::string::npos) {
      error = "invalid line " + std::to_string(line_num) + ": " + clean;
      return false;
    }

    const auto key   = trim(clean.substr(0, eq_pos));
    const auto value = trim(clean.substr(eq_pos + 1));

    // ---- [audio] --------------------------------------------------------
    if (section == "audio") {
      if      (key == "input_device")       config.audio.input_device = value;
      else if (key == "output_device")      config.audio.output_device = value;
      else if (key == "sample_rate_hz")     parse_number(value, config.audio.sample_rate_hz);
      else if (key == "frames_per_buffer")  parse_number(value, config.audio.frames_per_buffer);
      else if (key == "input_channels")     parse_number(value, config.audio.input_channels);
      else if (key == "output_channels")    parse_number(value, config.audio.output_channels);
      else if (key == "input_device_id")    parse_number(value, config.audio.input_device_id);
      else if (key == "output_device_id")   parse_number(value, config.audio.output_device_id);
      else if (key == "input_ring_capacity") parse_number(value, config.audio.input_ring_capacity);
      else if (key == "output_ring_capacity") parse_number(value, config.audio.output_ring_capacity);

    // ---- [vad] ----------------------------------------------------------
    } else if (section == "vad") {
      if      (key == "engine")                config.vad.engine = value;
      else if (key == "threshold")             parse_number(value, config.vad.threshold);
      else if (key == "silence_duration_ms")   parse_number(value, config.vad.silence_duration_ms);
      else if (key == "max_chunk_duration_ms") parse_number(value, config.vad.max_chunk_duration_ms);
      else if (key == "leading_pad_ms")        parse_number(value, config.vad.leading_pad_ms);
      else if (key == "trailing_pad_ms")       parse_number(value, config.vad.trailing_pad_ms);

    // ---- [asr] ----------------------------------------------------------
    } else if (section == "asr") {
      if      (key == "engine")             config.asr.engine = value;
      else if (key == "model_path")         config.asr.model_path = value;
      else if (key == "language")           config.asr.language = value;
      else if (key == "translate")          parse_bool(value, config.asr.translate);
      else if (key == "beam_size")          parse_number(value, config.asr.beam_size);
      else if (key == "enable_gpu")         parse_bool(value, config.asr.enable_gpu);
      else if (key == "gpu_enabled")        parse_bool(value, config.asr.enable_gpu);
      else if (key == "quantization")       config.asr.quantization = value;
      else if (key == "use_domain_prompt")  parse_bool(value, config.asr.use_domain_prompt);
      else if (key == "max_context_tokens") parse_number(value, config.asr.max_context_tokens);
      else if (key == "chunk_ms")           parse_number(value, config.asr.chunk_ms);
      else if (key == "hop_ms")             parse_number(value, config.asr.hop_ms);
      else if (key == "stability_threshold") parse_number(value, config.asr.stability_threshold);
      else if (key == "force_commit_ms")    parse_number(value, config.asr.force_commit_ms);
      // Legacy alias
      else if (key == "backend")            config.asr.engine = value;

    // ---- [tts] ----------------------------------------------------------
    } else if (section == "tts") {
      if      (key == "engine")                    config.tts.engine = value;
      else if (key == "backend")                   config.tts.engine = value;  // legacy alias
      else if (key == "model_path")                config.tts.model_path = value;
      else if (key == "piper_data_path")           config.tts.piper_data_path = value;
      else if (key == "speaker_id")                parse_number(value, config.tts.speaker_id);
      else if (key == "enable_gpu")                parse_bool(value, config.tts.enable_gpu);
      else if (key == "gpu_enabled")               parse_bool(value, config.tts.enable_gpu);
      else if (key == "output_sample_rate")        parse_number(value, config.tts.output_sample_rate);
      else if (key == "fallback_engine")           config.tts.fallback_engine = value;
      else if (key == "pronunciation_hints_enabled") parse_bool(value, config.tts.pronunciation_hints_enabled);
      else if (key == "hints_path")                config.tts.hints_path = value;
      else if (key == "speaker_reference")         config.tts.speaker_reference = value;

    // ---- [queues] -------------------------------------------------------
    } else if (section == "queues") {
      if      (key == "ingest_to_asr_capacity")  parse_number(value, config.queues.ingest_to_asr_capacity);
      else if (key == "asr_to_text_capacity")    parse_number(value, config.queues.asr_to_text_capacity);
      else if (key == "text_to_tts_capacity")    parse_number(value, config.queues.text_to_tts_capacity);
      // Legacy aliases
      else if (key == "asr_to_commit_capacity")  parse_number(value, config.queues.asr_to_commit_capacity);
      else if (key == "commit_to_tts_capacity")  parse_number(value, config.queues.commit_to_tts_capacity);

    // ---- [pipeline] -----------------------------------------------------
    } else if (section == "pipeline") {
      if      (key == "max_queue_depth")       parse_number(value, config.pipeline.max_queue_depth);
      else if (key == "drop_policy")           config.pipeline.drop_policy = parse_drop_policy(value);
      else if (key == "max_latency_ms")        parse_number(value, config.pipeline.max_latency_ms);
      else if (key == "warning_threshold_ms")  parse_number(value, config.pipeline.warning_threshold_ms);
      else if (key == "critical_threshold_ms") parse_number(value, config.pipeline.critical_threshold_ms);
      else if (key == "stale_after_n_newer")   parse_number(value, config.pipeline.stale_after_n_newer);
      else if (key == "stale_after_ms")        parse_number(value, config.pipeline.stale_after_ms);

    // ---- [gpu] ----------------------------------------------------------
    } else if (section == "gpu") {
      if      (key == "enabled")                         parse_bool(value, config.gpu.enabled);
      else if (key == "device_id")                       parse_number(value, config.gpu.device_id);
      else if (key == "asr_priority")                    parse_bool(value, config.gpu.asr_priority);
      else if (key == "tts_cpu_fallback_on_contention")  parse_bool(value, config.gpu.tts_cpu_fallback_on_contention);

    // ---- [domain] -------------------------------------------------------
    } else if (section == "domain") {
      if      (key == "glossary_path")             config.domain.glossary_path = value;
      else if (key == "session_terms_enabled")     parse_bool(value, config.domain.session_terms_enabled);
      else if (key == "pronunciation_hints")       parse_bool(value, config.domain.pronunciation_hints);
      else if (key == "initial_prompt_template")   config.domain.initial_prompt_template = value;
      else if (key == "technical_glossary")        config.domain.technical_glossary = parse_csv_list(value);
      else if (key == "frequent_phrases")          config.domain.frequent_phrases = parse_csv_list(value);
      else if (key == "session_terms_limit")       parse_number(value, config.domain.session_terms_limit);

    // ---- [resilience] ---------------------------------------------------
    } else if (section == "resilience") {
      if      (key == "enable_degradation")          parse_bool(value, config.resilience.enable_degradation);
      else if (key == "gpu_failure_action")          config.resilience.gpu_failure_action = value;
      else if (key == "passthrough_on_total_failure") parse_bool(value, config.resilience.passthrough_on_total_failure);

    // ---- [logging] ------------------------------------------------------
    } else if (section == "logging") {
      if      (key == "level")   config.logging.level = parse_log_level(value);
      else if (key == "file")    config.logging.file = value;
      else if (key == "console") parse_bool(value, config.logging.console);
      else if (key == "async")   parse_bool(value, config.logging.async);

    // ---- [telemetry] ----------------------------------------------------
    } else if (section == "telemetry") {
      if      (key == "enabled")              parse_bool(value, config.telemetry.enabled);
      else if (key == "report_interval_ms")   parse_number(value, config.telemetry.report_interval_ms);
      else if (key == "log_per_utterance")    parse_bool(value, config.telemetry.log_per_utterance);

    // ---- [runtime] (internal, not in TOML template) --------------------
    } else if (section == "runtime") {
      if      (key == "use_simulated_audio")   parse_bool(value, config.runtime.use_simulated_audio);
      else if (key == "run_duration_seconds")  parse_number(value, config.runtime.run_duration_seconds);
    }
  }

  return validate_config(config, error);
}

bool validate_config(const AppConfig& config, std::string& error) {
  if (config.audio.sample_rate_hz == 0 || config.audio.frames_per_buffer == 0) {
    error = "audio.sample_rate_hz and audio.frames_per_buffer must be > 0";
    return false;
  }

  if (config.asr.chunk_ms > 0 && config.asr.hop_ms > 0 &&
      config.asr.hop_ms > config.asr.chunk_ms) {
    error = "asr.hop_ms must be <= asr.chunk_ms";
    return false;
  }

  if (config.asr.stability_threshold < 0.0F || config.asr.stability_threshold > 1.0F) {
    error = "asr.stability_threshold must be in [0,1]";
    return false;
  }

  if (config.audio.input_ring_capacity < 8 || config.audio.output_ring_capacity < 8) {
    error = "audio ring capacities must be >= 8";
    return false;
  }

  if (config.queues.ingest_to_asr_capacity < 4) {
    error = "pipeline queue capacities must be >= 4";
    return false;
  }

  if (config.vad.silence_duration_ms == 0) {
    error = "vad.silence_duration_ms must be > 0";
    return false;
  }

  if (config.pipeline.warning_threshold_ms >= config.pipeline.critical_threshold_ms) {
    error = "pipeline.warning_threshold_ms must be < pipeline.critical_threshold_ms";
    return false;
  }

  return true;
}

}  // namespace mev
