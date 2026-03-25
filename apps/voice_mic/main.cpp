#include <atomic>
#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#ifdef _WIN32
#  include <windows.h>
#else
#  include <csignal>
#  include <dlfcn.h>
#endif

#include "mev/asr/whisper_asr_stub.hpp"
#include "mev/audio/portaudio_input.hpp"
#include "mev/audio/portaudio_output.hpp"
#include "mev/audio/simulated_audio_input.hpp"
#include "mev/audio/simulated_audio_output.hpp"
#include "mev/config/app_config.hpp"
#include "mev/core/logger.hpp"
#include "mev/pipeline/pipeline_orchestrator.hpp"
#include "mev/tts/espeak_tts_engine.hpp"
#include "mev/tts/piper_tts_engine.hpp"
#include "mev/tts/stub_tts_engine.hpp"

#if defined(MEV_ENABLE_WHISPER_CPP)
#include "mev/asr/whisper_asr_engine.hpp"
#endif

// ---------------------------------------------------------------------------
// Global shutdown flag — set by OS signal / console-ctrl handler.
// ---------------------------------------------------------------------------
static std::atomic<bool> g_shutdown_requested{false};  // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)

enum class DeviceListMode : std::uint8_t {
  kNone,
  kInput,
  kOutput,
  kBoth,
};

struct SelfTestReport {
  std::vector<std::string> info_lines;
  std::vector<std::string> warning_lines;

  void info(std::string line) {
    info_lines.push_back(std::move(line));
  }

  void warn(std::string line) {
    warning_lines.push_back(std::move(line));
  }

  void print() const {
    for (const auto& line : info_lines) {
      std::cerr << "[INFO] self-test: " << line << "\n";
    }
    for (const auto& line : warning_lines) {
      std::cerr << "[WARN] self-test: " << line << "\n";
    }
    std::cerr << std::flush;
  }
};

static void install_signal_handlers() {
#ifdef _WIN32
  SetConsoleCtrlHandler(
      [](DWORD event) -> BOOL {
        if (event == CTRL_C_EVENT || event == CTRL_CLOSE_EVENT ||
            event == CTRL_BREAK_EVENT) {
          g_shutdown_requested.store(true, std::memory_order_relaxed);
          return TRUE;
        }
        return FALSE;
      },
      TRUE);
#else
  struct sigaction sa{};
  sa.sa_handler = [](int) {
    g_shutdown_requested.store(true, std::memory_order_relaxed);
  };
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = SA_RESTART;
  sigaction(SIGINT,  &sa, nullptr);
  sigaction(SIGTERM, &sa, nullptr);
#endif
}

static void print_usage(const char* prog) {
  std::cout
      << "Usage: " << prog << " [--config <path>] [--self-test] [--list-devices <input|output|both>] "
      << "[--<section>.<key> <value>] [--help]\n"
      << "\n"
      << "Options:\n"
      << "  --config <path>           Config file (default: config/pipeline.toml)\n"
      << "  --self-test               Validate config, models, selected audio backend, ASR, and TTS\n"
      << "  --list-devices <mode>     List PortAudio devices for input, output, or both and exit\n"
      << "  --<section>.<key> <value> Override any config field after TOML load\n"
      << "                            e.g. --audio.sample_rate_hz 48000\n"
      << "                                 --tts.engine espeak\n"
      << "                                 --runtime.run_duration_seconds 60\n"
      << "  --help                    Print this help and exit\n";
}

static bool cuda_driver_available() {
#ifdef _WIN32
  HMODULE module = LoadLibraryA("nvcuda.dll");
  if (module == nullptr) {
    return false;
  }
  FreeLibrary(module);
  return true;
#else
  void* handle = dlopen("libcuda.so.1", RTLD_LAZY | RTLD_LOCAL);
  if (handle == nullptr) {
    return false;
  }
  dlclose(handle);
  return true;
#endif
}

static bool ort_cuda_provider_dll_available(const std::string& argv0) {
#if !defined(_WIN32)
  (void)argv0;
  return false;
#else
  std::vector<std::filesystem::path> candidates;
  const auto exe_dir = std::filesystem::absolute(argv0).parent_path();
  candidates.push_back(exe_dir / "onnxruntime_providers_cuda.dll");

  // MSVC deprecates getenv in favour of _dupenv_s (C4996). Suppress here;
  // this block is already inside #if defined(_WIN32).
#pragma warning(suppress : 4996)
  if (const char* ort_root = std::getenv("ONNXRUNTIME_ROOT")) {
    candidates.push_back(std::filesystem::path(ort_root) / "lib" / "onnxruntime_providers_cuda.dll");
  }

  for (const auto& candidate : candidates) {
    if (!candidate.empty() && std::filesystem::exists(candidate)) {
      return true;
    }
  }

  return false;
#endif
}

static bool make_audio_backends(const mev::AppConfig& cfg,
                                std::unique_ptr<mev::IAudioInput>& audio_input,
                                std::unique_ptr<mev::IAudioOutput>& audio_output,
                                std::string& error) {
  if (cfg.runtime.use_simulated_audio) {
    audio_input  = std::make_unique<mev::SimulatedAudioInput>(
        cfg.audio.sample_rate_hz, cfg.audio.input_channels, cfg.audio.frames_per_buffer);
    audio_output = std::make_unique<mev::SimulatedAudioOutput>(
        cfg.audio.sample_rate_hz, cfg.audio.output_channels, cfg.audio.frames_per_buffer);
    return true;
  }

#if defined(MEV_ENABLE_PORTAUDIO)
  audio_input  = std::make_unique<mev::PortAudioInput>(
      cfg.audio.sample_rate_hz, cfg.audio.input_channels,
      cfg.audio.frames_per_buffer, cfg.audio.input_device);
  audio_output = std::make_unique<mev::PortAudioOutput>(
      cfg.audio.sample_rate_hz, cfg.audio.output_channels,
      cfg.audio.frames_per_buffer, cfg.audio.output_device);
  return true;
#else
  error = "runtime.use_simulated_audio=false requires a build with MEV_ENABLE_PORTAUDIO=ON";
  return false;
#endif
}

#if defined(MEV_ENABLE_WHISPER_CPP)
static std::unique_ptr<mev::IASREngine> make_asr_engine(const mev::AppConfig& cfg) {
  return std::make_unique<mev::WhisperASREngine>(
      cfg.asr.model_path, cfg.asr.enable_gpu, cfg.asr.language,
      cfg.asr.translate, cfg.asr.quantization);
}
#endif

static std::unique_ptr<mev::ITTSEngine> make_tts_engine(const std::string& engine_name,
                                                        const mev::AppConfig& cfg,
                                                        std::string& error) {
  std::unique_ptr<mev::ITTSEngine> engine;
  if (engine_name == "stub") {
    engine = std::make_unique<mev::StubTTSEngine>();
  } else if (engine_name == "espeak") {
    engine = std::make_unique<mev::EspeakTTSEngine>();
  } else if (engine_name == "piper") {
    engine = std::make_unique<mev::PiperTTSEngine>();
  } else {
    error = "unsupported tts engine '" + engine_name + "'";
    return nullptr;
  }

  mev::TTSConfig tts_cfg;
  tts_cfg.engine             = engine_name;
  tts_cfg.model_path         = cfg.tts.model_path;
  tts_cfg.piper_data_path    = cfg.tts.piper_data_path;
  tts_cfg.speaker_id         = cfg.tts.speaker_id;
  tts_cfg.gpu_enabled        = cfg.tts.enable_gpu;
  tts_cfg.output_sample_rate = cfg.tts.output_sample_rate;
  tts_cfg.fallback_engine    = cfg.tts.fallback_engine;

  if (!engine->initialize(tts_cfg, error)) {
    return nullptr;
  }

  return engine;
}

static bool requires_piper_runtime(const mev::AppConfig& cfg) {
  return cfg.tts.engine == "piper" || cfg.tts.fallback_engine == "piper";
}

static bool validate_model_paths(const mev::AppConfig& cfg, std::string& error) {
  if (cfg.asr.model_path.empty() || !std::filesystem::exists(cfg.asr.model_path)) {
    error = "ASR model path not found: " + cfg.asr.model_path;
    return false;
  }

  if (requires_piper_runtime(cfg)) {
    if (cfg.tts.model_path.empty() || !std::filesystem::exists(cfg.tts.model_path)) {
      error = "Piper model path not found: " + cfg.tts.model_path;
      return false;
    }
    if (cfg.tts.piper_data_path.empty() || !std::filesystem::exists(cfg.tts.piper_data_path)) {
      error = "Piper JSON path not found: " + cfg.tts.piper_data_path;
      return false;
    }
  }

  return true;
}

static bool validate_onnxruntime_runtime(const mev::AppConfig& cfg, const std::string& argv0,
                                         std::string& error) {
  if (!requires_piper_runtime(cfg)) {
    return true;
  }

#if !defined(MEV_ENABLE_ONNXRUNTIME)
  (void)argv0;
  error = "Piper requires a build with MEV_ENABLE_ONNXRUNTIME=ON";
  return false;
#else
#ifdef _WIN32
  std::vector<std::filesystem::path> candidates;
  const auto exe_dir = std::filesystem::absolute(argv0).parent_path();
  candidates.push_back(exe_dir / "onnxruntime.dll");

#pragma warning(suppress : 4996)
  if (const char* ort_root = std::getenv("ONNXRUNTIME_ROOT")) {
    candidates.push_back(std::filesystem::path(ort_root) / "lib" / "onnxruntime.dll");
  }

  for (const auto& candidate : candidates) {
    if (!candidate.empty() && std::filesystem::exists(candidate)) {
      return true;
    }
  }

  error = "ONNX Runtime DLL not found. Checked executable directory and ONNXRUNTIME_ROOT.";
  return false;
#else
  (void)argv0;
  return true;
#endif
#endif
}

static bool validate_audio_backend(const mev::AppConfig& cfg, SelfTestReport& report,
                                   std::string& error) {
  std::unique_ptr<mev::IAudioInput> audio_input;
  std::unique_ptr<mev::IAudioOutput> audio_output;
  if (!make_audio_backends(cfg, audio_input, audio_output, error)) {
    return false;
  }

  const bool output_started = audio_output->start(
      [](float* output, std::size_t frames, std::uint16_t channels, std::uint32_t /*sample_rate*/) {
        std::fill(output, output + frames * channels, 0.0F);
      });
  if (!output_started) {
    error = "failed to start audio output backend '" + audio_output->name() + "'";
    audio_output->stop();
    audio_input->stop();
    return false;
  }

  const bool input_started = audio_input->start(
      [](const float* /*input*/, std::size_t /*frames*/, std::uint16_t /*channels*/,
         std::uint32_t /*sample_rate*/) {});
  if (!input_started) {
    error = "failed to start audio input backend '" + audio_input->name() + "'";
    audio_output->stop();
    audio_input->stop();
    return false;
  }

  audio_input->stop();
  audio_output->stop();
  report.info("audio input=" + audio_input->name() +
              " output=" + audio_output->name() +
              " simulated=" + std::string(cfg.runtime.use_simulated_audio ? "true" : "false"));
  return true;
}

static bool validate_asr_backend(const mev::AppConfig& cfg, SelfTestReport& report,
                                 std::string& error) {
#if !defined(MEV_ENABLE_WHISPER_CPP)
  (void)cfg;
  (void)report;
  error = "whisper.cpp is not compiled in (MEV_ENABLE_WHISPER_CPP=OFF)";
  return false;
#else
  auto engine = make_asr_engine(cfg);
  if (!engine) {
    error = "failed to create ASR engine";
    return false;
  }

  if (engine->warmup(error)) {
    report.info("asr backend=" + engine->name() + " " + engine->runtime_summary());
    return true;
  }

  if (cfg.asr.enable_gpu && cfg.resilience.gpu_failure_action == "fallback_cpu") {
    report.warn("ASR GPU warmup failed, retrying on CPU: " + error);
    auto cpu_cfg = cfg;
    cpu_cfg.asr.enable_gpu = false;
    if (cpu_cfg.asr.quantization == "f16") {
      cpu_cfg.asr.quantization = "q5_1";
    }

    auto cpu_engine = make_asr_engine(cpu_cfg);
    if (!cpu_engine) {
      error = "failed to create CPU fallback ASR engine";
      return false;
    }
    if (!cpu_engine->warmup(error)) {
      return false;
    }
    report.info("asr backend=" + cpu_engine->name() + " " + cpu_engine->runtime_summary());
    return true;
  }

  return false;
#endif
}

static bool validate_tts_backend(const mev::AppConfig& cfg, SelfTestReport& report,
                                 std::string& error) {
  auto validate_engine = [&](const std::string& engine_name, std::string& out_error) -> bool {
    auto engine = make_tts_engine(engine_name, cfg, out_error);
    if (!engine) {
      return false;
    }

    engine->warmup();

    std::vector<float> pcm;
    if (!engine->synthesize("Self test voice output", pcm)) {
      out_error = "TTS engine '" + engine_name + "' failed to synthesize audio";
      engine->shutdown();
      return false;
    }
    if (pcm.empty()) {
      out_error = "TTS engine '" + engine_name + "' produced empty PCM";
      engine->shutdown();
      return false;
    }

    report.info("tts backend=" + engine->engine_name() + " " + engine->runtime_summary());
    engine->shutdown();
    return true;
  };

  if (validate_engine(cfg.tts.engine, error)) {
    return true;
  }

  if (!cfg.tts.fallback_engine.empty() && cfg.tts.fallback_engine != cfg.tts.engine) {
    report.warn("primary TTS self-test failed, trying fallback '" +
                cfg.tts.fallback_engine + "'");
    return validate_engine(cfg.tts.fallback_engine, error);
  }

  return false;
}

static bool run_self_test(const mev::AppConfig& cfg, const std::string& argv0,
                          SelfTestReport& report, std::string& error) {
  const bool wants_gpu = cfg.asr.enable_gpu || cfg.tts.enable_gpu;
  if (wants_gpu) {
    report.info(std::string("cuda driver runtime=") +
                (cuda_driver_available() ? "present" : "missing"));
  }

  if (requires_piper_runtime(cfg) && cfg.tts.enable_gpu) {
    report.info(std::string("onnx cuda provider dll=") +
                (ort_cuda_provider_dll_available(argv0) ? "present" : "missing"));
  }

  if (!validate_model_paths(cfg, error)) {
    return false;
  }
  if (!validate_onnxruntime_runtime(cfg, argv0, error)) {
    return false;
  }
  if (!validate_audio_backend(cfg, report, error)) {
    return false;
  }
  if (!validate_asr_backend(cfg, report, error)) {
    return false;
  }
  if (!validate_tts_backend(cfg, report, error)) {
    return false;
  }
  return true;
}

static bool parse_device_list_mode(const std::string& value, DeviceListMode& mode) {
  if (value == "input") {
    mode = DeviceListMode::kInput;
    return true;
  }
  if (value == "output") {
    mode = DeviceListMode::kOutput;
    return true;
  }
  if (value == "both") {
    mode = DeviceListMode::kBoth;
    return true;
  }
  return false;
}

static bool list_audio_devices(DeviceListMode mode, std::string& error) {
#if defined(MEV_ENABLE_PORTAUDIO)
  if (mode == DeviceListMode::kInput || mode == DeviceListMode::kBoth) {
    mev::PortAudioInput::list_devices();
  }
  if (mode == DeviceListMode::kOutput || mode == DeviceListMode::kBoth) {
    mev::PortAudioOutput::list_devices();
  }
  return true;
#else
  (void)mode;
  error = "--list-devices requires a build with MEV_ENABLE_PORTAUDIO=ON";
  return false;
#endif
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
    else if (key == "preview_engine")      cfg.tts.preview_engine = value;
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
  bool self_test = false;
  DeviceListMode device_list_mode = DeviceListMode::kNone;

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

    if (arg == "--self-test") {
      self_test = true;
      continue;
    }

    if (arg == "--list-devices") {
      if (i + 1 >= argc) {
        std::cerr << "[ERROR] --list-devices requires one of: input, output, both\n";
        return 1;
      }
      if (!parse_device_list_mode(argv[++i], device_list_mode)) {
        std::cerr << "[ERROR] invalid value for --list-devices. Use: input, output, both\n";
        return 1;
      }
      continue;
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

  if (device_list_mode != DeviceListMode::kNone) {
    mev::Logger::instance().set_level(mev::LogLevel::kInfo);
    std::string error;
    if (!list_audio_devices(device_list_mode, error)) {
      std::cerr << "[ERROR] " << error << "\n";
      return 1;
    }
    return 0;
  }

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

  if (self_test) {
    mev::Logger::instance().set_level(config.logging.level);
    SelfTestReport report;
    if (!run_self_test(config, argv[0], report, error)) {
      report.print();
      std::cerr << "[FAIL] self-test: " << error << "\n";
      return 1;
    }
    report.print();
    std::cout << "[PASS] self-test: config, models, audio backend, ASR, and TTS validated\n";
    return 0;
  }

  // Install signal handlers for clean shutdown.
  install_signal_handlers();

  // Start pipeline.
  mev::PipelineOrchestrator orchestrator(config);
  if (!orchestrator.start()) {
    MEV_LOG_ERROR("failed to start pipeline");
    return 2;
  }

  // Run until duration expires or signal received.
  // run_duration_seconds == 0 means run indefinitely until SIGINT/SIGTERM.
  const bool run_forever = (config.runtime.run_duration_seconds == 0);
  // Extra parens around ::max prevent windows.h's max() macro from expanding.
  const auto deadline = run_forever
      ? (std::chrono::steady_clock::time_point::max)()
      : std::chrono::steady_clock::now() +
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
