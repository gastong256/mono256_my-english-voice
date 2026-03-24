#include <cassert>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>

#include "mev/config/app_config.hpp"

static void test_default_config() {
  const mev::AppConfig cfg = mev::default_config();
  assert(cfg.audio.sample_rate_hz > 0 && "default sample_rate_hz must be > 0");
  assert(cfg.audio.frames_per_buffer > 0 && "default frames_per_buffer must be > 0");
  assert(cfg.audio.input_ring_capacity >= 8 && "default input_ring_capacity must be >= 8");
  assert(cfg.vad.silence_duration_ms > 0 && "default silence_duration_ms must be > 0");
  assert(cfg.pipeline.warning_threshold_ms < cfg.pipeline.critical_threshold_ms &&
         "warning < critical must hold");
  std::cout << "[PASS] test_default_config\n";
}

static void test_load_pipeline_toml() {
  // Look for config/pipeline.toml relative to common run locations.
  std::string path;
  for (const auto& candidate : {
           "config/pipeline.toml",
           "../config/pipeline.toml",
           "../../config/pipeline.toml",
       }) {
    if (std::filesystem::exists(candidate)) {
      path = candidate;
      break;
    }
  }

  if (path.empty()) {
    std::cout << "[SKIP] test_load_pipeline_toml: config/pipeline.toml not found\n";
    return;
  }

  mev::AppConfig cfg = mev::default_config();
  std::string error;
  const bool ok = mev::load_config_from_file(path, cfg, error);
  if (!ok) {
    std::cerr << "[FAIL] test_load_pipeline_toml: " << error << "\n";
    std::exit(1);
  }

  // Verify a few key fields that must be set in the default TOML.
  assert(cfg.audio.sample_rate_hz > 0 && "sample_rate_hz must be > 0 after TOML load");
  assert(cfg.audio.frames_per_buffer > 0 && "frames_per_buffer must be > 0 after TOML load");
  assert(!cfg.tts.engine.empty() && "tts.engine must not be empty");
  assert(!cfg.asr.model_path.empty() && "asr.model_path must not be empty");
  assert(cfg.pipeline.warning_threshold_ms < cfg.pipeline.critical_threshold_ms &&
         "warning < critical must hold after TOML load");

  std::cout << "[PASS] test_load_pipeline_toml (sr=" << cfg.audio.sample_rate_hz
            << " tts=" << cfg.tts.engine << ")\n";
}

static void test_load_windows_pipeline_toml() {
  std::string path;
  for (const auto& candidate : {
           "config/pipeline.windows.toml",
           "../config/pipeline.windows.toml",
           "../../config/pipeline.windows.toml",
       }) {
    if (std::filesystem::exists(candidate)) {
      path = candidate;
      break;
    }
  }

  if (path.empty()) {
    std::cout << "[SKIP] test_load_windows_pipeline_toml: config/pipeline.windows.toml not found\n";
    return;
  }

  mev::AppConfig cfg = mev::default_config();
  std::string error;
  const bool ok = mev::load_config_from_file(path, cfg, error);
  if (!ok) {
    std::cerr << "[FAIL] test_load_windows_pipeline_toml: " << error << "\n";
    std::exit(1);
  }

  assert(!cfg.runtime.use_simulated_audio &&
         "Windows config must opt into real audio");
  assert(cfg.tts.engine == "espeak" &&
         "Phase 4 Windows config must keep eSpeak as the real TTS baseline");
  assert(!cfg.asr.enable_gpu &&
         "Windows config must default to CPU-friendly ASR in the current phase");
  assert(cfg.audio.output_device == "CABLE Input" &&
         "Windows config must target VB-Cable by default");

  std::cout << "[PASS] test_load_windows_pipeline_toml (tts=" << cfg.tts.engine
            << " gpu=" << cfg.asr.enable_gpu << ")\n";
}

static void test_invalid_toml() {
  // Write a temp file with broken TOML.
  const std::string tmp = "/tmp/mev_test_invalid.toml";
  {
    // Create a file with invalid TOML syntax.
    FILE* f = std::fopen(tmp.c_str(), "w");
    if (f == nullptr) {
      std::cout << "[SKIP] test_invalid_toml: cannot write to /tmp\n";
      return;
    }
    std::fputs("[audio\nbroken = \n", f);
    std::fclose(f);
  }

  mev::AppConfig cfg = mev::default_config();
  std::string error;
  const bool ok = mev::load_config_from_file(tmp, cfg, error);
  assert(!ok && "loading invalid TOML must return false");
  assert(!error.empty() && "error string must be non-empty on failure");
  std::cout << "[PASS] test_invalid_toml (error='" << error << "')\n";

  std::remove(tmp.c_str());
}

static void test_missing_file() {
  mev::AppConfig cfg = mev::default_config();
  std::string error;
  const bool ok = mev::load_config_from_file("/nonexistent/path/config.toml", cfg, error);
  assert(!ok && "loading a missing file must return false");
  assert(!error.empty() && "error string must be non-empty on failure");
  std::cout << "[PASS] test_missing_file\n";
}

static void test_validate_catches_bad_range() {
  mev::AppConfig cfg = mev::default_config();
  cfg.vad.threshold = 1.5F;  // out of [0,1]
  std::string error;
  const bool ok = mev::validate_config(cfg, error);
  assert(!ok && "validate_config must reject threshold > 1");
  std::cout << "[PASS] test_validate_catches_bad_range\n";
}

int main() {
  test_default_config();
  test_load_pipeline_toml();
  test_load_windows_pipeline_toml();
  test_invalid_toml();
  test_missing_file();
  test_validate_catches_bad_range();
  std::cout << "All test_toml_config tests passed.\n";
  return 0;
}
