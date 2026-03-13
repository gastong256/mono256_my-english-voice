#include <atomic>
#include <csignal>
#include <cstdlib>
#include <iostream>
#include <sstream>
#include <string>

#include "mev/app/application.hpp"
#include "mev/config/app_config.hpp"
#include "mev/core/logger.hpp"
#include "mev/pipeline/pipeline_orchestrator.hpp"

// ---------------------------------------------------------------------------
// Global shutdown flag set by SIGINT/SIGTERM handler.
// ---------------------------------------------------------------------------
static std::atomic<bool> g_shutdown_requested{false};  // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)

static void signal_handler(int /*signum*/) {
  g_shutdown_requested.store(true, std::memory_order_relaxed);
}

static void print_usage(const char* prog) {
  std::cout
      << "Usage: " << prog << " [--config <path>] [--<section>.<key> <value>] [--help]\n"
      << "\n"
      << "Options:\n"
      << "  --config <path>           Config file (default: config/pipeline.toml)\n"
      << "  --<section>.<key> <value> Override any config field after TOML load\n"
      << "                            e.g. --audio.sample_rate_hz 48000\n"
      << "                                 --tts.engine espeak\n"
      << "                                 --runtime.run_duration_seconds 60\n"
      << "  --help                    Print this help and exit\n";
}

// Apply a dot-separated key=value override to the config.
// Silently ignores unknown keys to stay forward-compatible.
static void apply_override(mev::AppConfig& cfg, const std::string& dotkey,
                           const std::string& value) {
  const auto dot = dotkey.find('.');
  if (dot == std::string::npos) {
    std::cerr << "[WARN] ignoring malformed override (no dot): " << dotkey << "\n";
    return;
  }
  const auto section = dotkey.substr(0, dot);
  const auto key     = dotkey.substr(dot + 1);

  auto to_uint32 = [&](std::uint32_t& out) {
    std::istringstream ss(value);
    std::uint32_t v{};
    if (ss >> v) out = v;
  };
  auto to_bool = [&](bool& out) {
    if (value == "true" || value == "1" || value == "yes") { out = true; }
    else if (value == "false" || value == "0" || value == "no") { out = false; }
  };

  if (section == "audio") {
    if      (key == "input_device")        cfg.audio.input_device = value;
    else if (key == "output_device")       cfg.audio.output_device = value;
    else if (key == "sample_rate_hz")      to_uint32(cfg.audio.sample_rate_hz);
    else if (key == "frames_per_buffer")   to_uint32(cfg.audio.frames_per_buffer);
  } else if (section == "asr") {
    if      (key == "model_path")          cfg.asr.model_path = value;
    else if (key == "language")            cfg.asr.language = value;
    else if (key == "translate")           to_bool(cfg.asr.translate);
    else if (key == "enable_gpu")          to_bool(cfg.asr.enable_gpu);
    else if (key == "use_domain_prompt")   to_bool(cfg.asr.use_domain_prompt);
  } else if (section == "tts") {
    if      (key == "engine")              cfg.tts.engine = value;
    else if (key == "model_path")          cfg.tts.model_path = value;
    else if (key == "enable_gpu")          to_bool(cfg.tts.enable_gpu);
    else if (key == "fallback_engine")     cfg.tts.fallback_engine = value;
  } else if (section == "vad") {
    if      (key == "engine")              cfg.vad.engine = value;
  } else if (section == "logging") {
    if      (key == "level")               cfg.logging.level =
        (value == "debug" ? mev::LogLevel::kDebug :
         value == "warn"  ? mev::LogLevel::kWarn  :
         value == "error" ? mev::LogLevel::kError  : mev::LogLevel::kInfo);
  } else if (section == "runtime") {
    if      (key == "run_duration_seconds") to_uint32(cfg.runtime.run_duration_seconds);
    else if (key == "use_simulated_audio")  to_bool(cfg.runtime.use_simulated_audio);
  }
}

int main(int argc, char** argv) {
  std::string config_path = "config/pipeline.toml";

  // Collect overrides to apply after TOML load.
  struct Override { std::string key; std::string value; };
  std::vector<Override> overrides;

  // Parse arguments.
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];

    if (arg == "--help" || arg == "-h") {
      print_usage(argv[0]);
      return 0;
    }

    if (arg == "--config") {
      if (i + 1 >= argc) {
        std::cerr << "[ERROR] --config requires a path argument\n";
        return 1;
      }
      config_path = argv[++i];
      continue;
    }

    // --<section>.<key> <value>
    if (arg.size() > 2 && arg[0] == '-' && arg[1] == '-') {
      const std::string dotkey = arg.substr(2);
      if (i + 1 >= argc) {
        std::cerr << "[ERROR] " << arg << " requires a value argument\n";
        return 1;
      }
      overrides.push_back({dotkey, argv[++i]});
      continue;
    }

    // Legacy positional: first positional arg is the config path.
    if (arg[0] != '-') {
      config_path = arg;
      continue;
    }

    std::cerr << "[WARN] unknown argument ignored: " << arg << "\n";
  }

  // Install signal handlers for clean shutdown.
  std::signal(SIGINT,  signal_handler);
  std::signal(SIGTERM, signal_handler);

  // Load config.
  mev::AppConfig config = mev::default_config();
  std::string error;
  if (!mev::load_config_from_file(config_path, config, error)) {
    std::cerr << "[ERROR] failed to load config '" << config_path << "': " << error << "\n";
    return 1;
  }

  // Apply CLI overrides.
  for (const auto& ov : overrides) {
    apply_override(config, ov.key, ov.value);
  }

  // Validate after overrides.
  if (!mev::validate_config(config, error)) {
    std::cerr << "[ERROR] invalid config after overrides: " << error << "\n";
    return 1;
  }

  // Start pipeline.
  mev::PipelineOrchestrator orchestrator(config);
  if (!orchestrator.start()) {
    MEV_LOG_ERROR("failed to start pipeline");
    return 2;
  }

  // Run until duration expires or signal received.
  const auto deadline = std::chrono::steady_clock::now() +
                        std::chrono::seconds(config.runtime.run_duration_seconds);
  while (!g_shutdown_requested.load(std::memory_order_relaxed) &&
         std::chrono::steady_clock::now() < deadline) {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }

  if (g_shutdown_requested.load(std::memory_order_relaxed)) {
    MEV_LOG_INFO("SIGINT/SIGTERM received — shutting down");
  }

  orchestrator.stop();

  const auto snapshot = orchestrator.metrics_snapshot();
  MEV_LOG_INFO("shutdown metrics queue_drops=", snapshot.queue_drops,
               " output_underruns=", snapshot.output_underruns,
               " stale_cancelled=", snapshot.stale_cancelled);

  return 0;
}
