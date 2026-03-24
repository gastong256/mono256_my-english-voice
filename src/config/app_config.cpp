#include "mev/config/app_config.hpp"

#include <filesystem>
#include <sstream>

#include "mev/core/logger.hpp"

#define TOML_EXCEPTIONS 1
#include <toml++/toml.hpp>

namespace mev {

namespace {

LogLevel parse_log_level(const std::string& value) {
  if (value == "error") return LogLevel::kError;
  if (value == "warn")  return LogLevel::kWarn;
  if (value == "debug") return LogLevel::kDebug;
  return LogLevel::kInfo;
}

DropPolicy parse_drop_policy(const std::string& value) {
  if (value == "coalesce") return DropPolicy::kCoalesce;
  if (value == "none")     return DropPolicy::kNone;
  return DropPolicy::kDropOldest;
}

}  // namespace

AppConfig default_config() { return AppConfig{}; }

bool load_config_from_file(const std::string& path, AppConfig& config, std::string& error) {
  toml::table tbl;
  try {
    tbl = toml::parse_file(path);
  } catch (const toml::parse_error& e) {
    std::ostringstream oss;
    oss << "TOML parse error in '" << path << "': " << e.description()
        << " (line " << e.source().begin.line << ")";
    error = oss.str();
    return false;
  } catch (const std::exception& e) {
    error = std::string("failed to open config '") + path + "': " + e.what();
    return false;
  }

  // ---- [audio] ------------------------------------------------------------
  if (const auto* audio = tbl["audio"].as_table()) {
    config.audio.input_device  = audio->get_as<std::string>("input_device")
                                     ? audio->at("input_device").value_or(config.audio.input_device)
                                     : config.audio.input_device;
    config.audio.output_device = audio->get_as<std::string>("output_device")
                                     ? audio->at("output_device").value_or(config.audio.output_device)
                                     : config.audio.output_device;
    // The TOML file uses "sample_rate" and "frame_size"; also accept the longer names.
    if (const auto v = (*audio)["sample_rate"].value<int64_t>()) {
      config.audio.sample_rate_hz = static_cast<std::uint32_t>(*v);
    } else if (const auto v2 = (*audio)["sample_rate_hz"].value<int64_t>()) {
      config.audio.sample_rate_hz = static_cast<std::uint32_t>(*v2);
    }
    if (const auto v = (*audio)["frame_size"].value<int64_t>()) {
      config.audio.frames_per_buffer = static_cast<std::uint32_t>(*v);
    } else if (const auto v2 = (*audio)["frames_per_buffer"].value<int64_t>()) {
      config.audio.frames_per_buffer = static_cast<std::uint32_t>(*v2);
    }
    if (const auto v = (*audio)["channels"].value<int64_t>()) {
      config.audio.input_channels  = static_cast<std::uint16_t>(*v);
      config.audio.output_channels = static_cast<std::uint16_t>(*v);
    }
    if (const auto v = (*audio)["input_channels"].value<int64_t>()) {
      config.audio.input_channels = static_cast<std::uint16_t>(*v);
    }
    if (const auto v = (*audio)["output_channels"].value<int64_t>()) {
      config.audio.output_channels = static_cast<std::uint16_t>(*v);
    }
    if (const auto v = (*audio)["input_ring_capacity"].value<int64_t>()) {
      config.audio.input_ring_capacity = static_cast<std::size_t>(*v);
    }
    if (const auto v = (*audio)["output_ring_capacity"].value<int64_t>()) {
      config.audio.output_ring_capacity = static_cast<std::size_t>(*v);
    }
  }

  // ---- [vad] --------------------------------------------------------------
  if (const auto* vad = tbl["vad"].as_table()) {
    config.vad.engine               = (*vad)["engine"].value_or(config.vad.engine);
    config.vad.threshold            = (*vad)["threshold"].value_or(config.vad.threshold);
    if (const auto v = (*vad)["silence_duration_ms"].value<int64_t>()) {
      config.vad.silence_duration_ms = static_cast<std::uint32_t>(*v);
    }
    if (const auto v = (*vad)["max_chunk_duration_ms"].value<int64_t>()) {
      config.vad.max_chunk_duration_ms = static_cast<std::uint32_t>(*v);
    }
    if (const auto v = (*vad)["leading_pad_ms"].value<int64_t>()) {
      config.vad.leading_pad_ms = static_cast<std::uint32_t>(*v);
    }
    if (const auto v = (*vad)["trailing_pad_ms"].value<int64_t>()) {
      config.vad.trailing_pad_ms = static_cast<std::uint32_t>(*v);
    }
  }

  // ---- [asr] --------------------------------------------------------------
  if (const auto* asr = tbl["asr"].as_table()) {
    config.asr.engine           = (*asr)["engine"].value_or(config.asr.engine);
    config.asr.model_path       = (*asr)["model_path"].value_or(config.asr.model_path);
    config.asr.language         = (*asr)["language"].value_or(config.asr.language);
    config.asr.translate        = (*asr)["translate"].value_or(config.asr.translate);
    config.asr.enable_gpu       = (*asr)["gpu_enabled"].value_or(
                                  (*asr)["enable_gpu"].value_or(config.asr.enable_gpu));
    config.asr.quantization     = (*asr)["quantization"].value_or(config.asr.quantization);
    config.asr.use_domain_prompt = (*asr)["use_domain_prompt"].value_or(config.asr.use_domain_prompt);
    if (const auto v = (*asr)["beam_size"].value<int64_t>()) {
      config.asr.beam_size = static_cast<std::uint32_t>(*v);
    }
    if (const auto v = (*asr)["max_context_tokens"].value<int64_t>()) {
      config.asr.max_context_tokens = static_cast<std::uint32_t>(*v);
    }
    if (const auto v = (*asr)["chunk_ms"].value<int64_t>()) {
      config.asr.chunk_ms = static_cast<std::uint32_t>(*v);
    }
    if (const auto v = (*asr)["hop_ms"].value<int64_t>()) {
      config.asr.hop_ms = static_cast<std::uint32_t>(*v);
    }
    config.asr.stability_threshold = (*asr)["stability_threshold"].value_or(
        config.asr.stability_threshold);
    if (const auto v = (*asr)["force_commit_ms"].value<int64_t>()) {
      config.asr.force_commit_ms = static_cast<std::uint32_t>(*v);
    }
  }

  // ---- [tts] --------------------------------------------------------------
  if (const auto* tts = tbl["tts"].as_table()) {
    config.tts.engine            = (*tts)["engine"].value_or(config.tts.engine);
    config.tts.model_path        = (*tts)["model_path"].value_or(config.tts.model_path);
    config.tts.piper_data_path   = (*tts)["piper_data_path"].value_or(config.tts.piper_data_path);
    config.tts.enable_gpu        = (*tts)["gpu_enabled"].value_or(
                                   (*tts)["enable_gpu"].value_or(config.tts.enable_gpu));
    config.tts.fallback_engine   = (*tts)["fallback_engine"].value_or(config.tts.fallback_engine);
    if (const auto v = (*tts)["speaker_id"].value<int64_t>()) {
      config.tts.speaker_id = static_cast<std::uint32_t>(*v);
    }
    if (const auto v = (*tts)["output_sample_rate"].value<int64_t>()) {
      config.tts.output_sample_rate = static_cast<std::uint32_t>(*v);
    }
    // Sub-table [tts.pronunciation_hints]
    if (const auto* hints = (*tts)["pronunciation_hints"].as_table()) {
      config.tts.pronunciation_hints_enabled =
          (*hints)["enabled"].value_or(config.tts.pronunciation_hints_enabled);
      config.tts.hints_path =
          (*hints)["hints_path"].value_or(config.tts.hints_path);
    }
  }

  // ---- [queues] -----------------------------------------------------------
  if (const auto* q = tbl["queues"].as_table()) {
    if (const auto v = (*q)["ingest_to_asr_capacity"].value<int64_t>()) {
      config.queues.ingest_to_asr_capacity = static_cast<std::size_t>(*v);
    }
    if (const auto v = (*q)["asr_to_text_capacity"].value<int64_t>()) {
      config.queues.asr_to_text_capacity = static_cast<std::size_t>(*v);
    }
    if (const auto v = (*q)["text_to_tts_capacity"].value<int64_t>()) {
      config.queues.text_to_tts_capacity = static_cast<std::size_t>(*v);
    }
  }

  // ---- [pipeline] ---------------------------------------------------------
  if (const auto* pl = tbl["pipeline"].as_table()) {
    if (const auto v = (*pl)["max_queue_depth"].value<int64_t>()) {
      config.pipeline.max_queue_depth = static_cast<std::size_t>(*v);
    }
    const auto drop_str = (*pl)["drop_policy"].value_or(std::string{});
    if (!drop_str.empty()) {
      config.pipeline.drop_policy = parse_drop_policy(drop_str);
    }
    if (const auto v = (*pl)["max_latency_ms"].value<int64_t>()) {
      config.pipeline.max_latency_ms = static_cast<std::uint32_t>(*v);
    }
    if (const auto v = (*pl)["warning_threshold_ms"].value<int64_t>()) {
      config.pipeline.warning_threshold_ms = static_cast<std::uint32_t>(*v);
    }
    if (const auto v = (*pl)["critical_threshold_ms"].value<int64_t>()) {
      config.pipeline.critical_threshold_ms = static_cast<std::uint32_t>(*v);
    }
    if (const auto v = (*pl)["stale_after_n_newer"].value<int64_t>()) {
      config.pipeline.stale_after_n_newer = static_cast<std::uint32_t>(*v);
    }
    if (const auto v = (*pl)["stale_after_ms"].value<int64_t>()) {
      config.pipeline.stale_after_ms = static_cast<std::uint32_t>(*v);
    }
  }

  // ---- [gpu] --------------------------------------------------------------
  if (const auto* gpu = tbl["gpu"].as_table()) {
    config.gpu.enabled                        = (*gpu)["enabled"].value_or(config.gpu.enabled);
    config.gpu.asr_priority                   = (*gpu)["asr_priority"].value_or(config.gpu.asr_priority);
    config.gpu.tts_cpu_fallback_on_contention =
        (*gpu)["tts_cpu_fallback_on_contention"].value_or(config.gpu.tts_cpu_fallback_on_contention);
    if (const auto v = (*gpu)["device_id"].value<int64_t>()) {
      config.gpu.device_id = static_cast<int>(*v);
    }
  }

  // ---- [domain] -----------------------------------------------------------
  if (const auto* dom = tbl["domain"].as_table()) {
    config.domain.glossary_path           = (*dom)["glossary_path"].value_or(config.domain.glossary_path);
    config.domain.session_terms_enabled   = (*dom)["session_terms_enabled"].value_or(
                                              config.domain.session_terms_enabled);
    config.domain.pronunciation_hints     = (*dom)["pronunciation_hints"].value_or(
                                              config.domain.pronunciation_hints);
    config.domain.initial_prompt_template = (*dom)["initial_prompt_template"].value_or(
                                              config.domain.initial_prompt_template);
    if (const auto v = (*dom)["session_terms_limit"].value<int64_t>()) {
      config.domain.session_terms_limit = static_cast<std::size_t>(*v);
    }
    // Parse domain term lists.
    if (const auto* arr = (*dom)["technical_glossary"].as_array()) {
      config.domain.technical_glossary.clear();
      arr->for_each([&](const auto& el) {
        if (const auto* s = el.as_string()) {
          config.domain.technical_glossary.push_back(s->get());
        }
      });
    }
    if (const auto* arr = (*dom)["frequent_phrases"].as_array()) {
      config.domain.frequent_phrases.clear();
      arr->for_each([&](const auto& el) {
        if (const auto* s = el.as_string()) {
          config.domain.frequent_phrases.push_back(s->get());
        }
      });
    }
  }

  // ---- [resilience] -------------------------------------------------------
  if (const auto* res = tbl["resilience"].as_table()) {
    config.resilience.enable_degradation        =
        (*res)["enable_degradation"].value_or(config.resilience.enable_degradation);
    config.resilience.gpu_failure_action        =
        (*res)["gpu_failure_action"].value_or(config.resilience.gpu_failure_action);
    config.resilience.passthrough_on_total_failure =
        (*res)["passthrough_on_total_failure"].value_or(
            config.resilience.passthrough_on_total_failure);
  }

  // ---- [logging] ----------------------------------------------------------
  if (const auto* log = tbl["logging"].as_table()) {
    const auto lvl_str = (*log)["level"].value_or(std::string{});
    if (!lvl_str.empty()) {
      config.logging.level = parse_log_level(lvl_str);
    }
    config.logging.file    = (*log)["file"].value_or(config.logging.file);
    config.logging.console = (*log)["console"].value_or(config.logging.console);
    config.logging.async   = (*log)["async"].value_or(config.logging.async);
  }

  // ---- [telemetry] --------------------------------------------------------
  if (const auto* tel = tbl["telemetry"].as_table()) {
    config.telemetry.enabled           = (*tel)["enabled"].value_or(config.telemetry.enabled);
    config.telemetry.log_per_utterance = (*tel)["log_per_utterance"].value_or(
                                           config.telemetry.log_per_utterance);
    if (const auto v = (*tel)["report_interval_ms"].value<int64_t>()) {
      config.telemetry.report_interval_ms = static_cast<std::uint32_t>(*v);
    }
  }

  // ---- [runtime] ----------------------------------------------------------
  if (const auto* rt = tbl["runtime"].as_table()) {
    config.runtime.use_simulated_audio  = (*rt)["use_simulated_audio"].value_or(
                                            config.runtime.use_simulated_audio);
    if (const auto v = (*rt)["run_duration_seconds"].value<int64_t>()) {
      config.runtime.run_duration_seconds = static_cast<std::uint32_t>(*v);
    }
  }

  // ---- Post-load warnings -------------------------------------------------
  if (!config.asr.model_path.empty() &&
      !std::filesystem::exists(config.asr.model_path)) {
    MEV_LOG_WARN("config: asr.model_path not found: '", config.asr.model_path, "'");
  }
  if (!config.tts.model_path.empty() &&
      !std::filesystem::exists(config.tts.model_path)) {
    MEV_LOG_WARN("config: tts.model_path not found: '", config.tts.model_path, "'");
  }

  return validate_config(config, error);
}

bool validate_config(const AppConfig& config, std::string& error) {
  if (config.audio.sample_rate_hz == 0 || config.audio.frames_per_buffer == 0) {
    error = "audio.sample_rate_hz and audio.frames_per_buffer must be > 0";
    return false;
  }

  if (config.vad.engine != "none" && config.vad.engine != "webrtcvad") {
    error = "vad.engine must be one of: none, webrtcvad";
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

  if (config.vad.threshold < 0.0F || config.vad.threshold > 1.0F) {
    error = "vad.threshold must be in [0,1]";
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
