#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#if !defined(_WIN32)
#include <sys/wait.h>
#endif

namespace fs = std::filesystem;

namespace {

struct CommandResult {
  int exit_code{0};
  std::string output;
};

fs::path g_test_binary_path;

std::string shell_quote(const fs::path& value) {
  return "\"" + value.string() + "\"";
}

std::string shell_quote(const std::string& value) {
  return "\"" + value + "\"";
}

fs::path voice_mic_executable_path() {
  const fs::path build_dir = g_test_binary_path.parent_path().parent_path();
#if defined(_WIN32)
  return build_dir / "apps" / "voice_mic" / "mev_voice_mic.exe";
#else
  return build_dir / "apps" / "voice_mic" / "mev_voice_mic";
#endif
}

int normalize_exit_code(int raw_code) {
#if defined(_WIN32)
  return raw_code;
#else
  if (WIFEXITED(raw_code)) {
    return WEXITSTATUS(raw_code);
  }
  return raw_code;
#endif
}

CommandResult run_voice_mic(const std::vector<std::string>& args) {
  const fs::path exe_path = voice_mic_executable_path();
  assert(fs::exists(exe_path) && "voice mic executable must exist before CLI contract tests run");

  const fs::path log_path = fs::temp_directory_path() /
                            ("mev_cli_contract_" + std::to_string(std::rand()) + ".log");

  std::string command = shell_quote(exe_path);
  for (const auto& arg : args) {
    command += " ";
    command += shell_quote(arg);
  }
  command += " > " + shell_quote(log_path) + " 2>&1";

  const int raw_exit_code = std::system(command.c_str());

  std::ifstream input(log_path);
  std::string output((std::istreambuf_iterator<char>(input)),
                     std::istreambuf_iterator<char>());
  std::error_code ec;
  fs::remove(log_path, ec);

  return CommandResult{
      .exit_code = normalize_exit_code(raw_exit_code),
      .output = std::move(output),
  };
}

fs::path write_temp_config_for_missing_model() {
  const fs::path path = fs::temp_directory_path() /
                        ("mev_cli_self_test_" + std::to_string(std::rand()) + ".toml");

  std::ofstream out(path);
  out
      << "[audio]\n"
      << "input_device = \"default\"\n"
      << "output_device = \"default\"\n"
      << "sample_rate = 16000\n"
      << "frame_size = 256\n"
      << "channels = 1\n"
      << "\n"
      << "[vad]\n"
      << "engine = \"none\"\n"
      << "\n"
      << "[asr]\n"
      << "engine = \"whisper\"\n"
      << "model_path = \"models/definitely-missing.bin\"\n"
      << "language = \"es\"\n"
      << "translate = true\n"
      << "gpu_enabled = false\n"
      << "quantization = \"q5_1\"\n"
      << "\n"
      << "[tts]\n"
      << "engine = \"stub\"\n"
      << "fallback_engine = \"stub\"\n"
      << "\n"
      << "[runtime]\n"
      << "use_simulated_audio = true\n"
      << "run_duration_seconds = 1\n";

  out.close();
  return path;
}

void test_help_mentions_public_flags() {
  const auto result = run_voice_mic({"--help"});
  assert(result.exit_code == 0 && "help must exit successfully");
  assert(result.output.find("--self-test") != std::string::npos &&
         "help output must mention --self-test");
  assert(result.output.find("--list-devices") != std::string::npos &&
         "help output must mention --list-devices");
}

void test_list_devices_rejects_invalid_mode() {
  const auto result = run_voice_mic({"--list-devices", "invalid"});
  assert(result.exit_code != 0 && "invalid --list-devices value must fail");
  assert(result.output.find("input, output, both") != std::string::npos &&
         "invalid --list-devices output must explain valid values");
}

void test_list_devices_reports_runtime_contract() {
  const auto result = run_voice_mic({"--list-devices", "both"});
#if defined(MEV_ENABLE_PORTAUDIO)
  assert(result.exit_code == 0 && "device listing must succeed when PortAudio is compiled in");
#else
  assert(result.exit_code != 0 && "device listing must fail when PortAudio is unavailable");
  assert(result.output.find("MEV_ENABLE_PORTAUDIO=ON") != std::string::npos &&
         "device listing failure must explain the PortAudio requirement");
#endif
}

void test_self_test_reports_missing_model_clearly() {
  const fs::path config_path = write_temp_config_for_missing_model();
  const auto result = run_voice_mic({"--self-test", "--config", config_path.string()});

  std::error_code ec;
  fs::remove(config_path, ec);

  assert(result.exit_code != 0 && "self-test must fail when the ASR model is missing");
  assert(result.output.find("[FAIL] self-test:") != std::string::npos &&
         "self-test failure must be prefixed clearly");
  assert(result.output.find("ASR model path not found") != std::string::npos &&
         "self-test failure must explain the missing ASR model");
}

}  // namespace

int main(int argc, char** argv) {
  assert(argc > 0);
  g_test_binary_path = fs::weakly_canonical(fs::path(argv[0]));

  test_help_mentions_public_flags();
  test_list_devices_rejects_invalid_mode();
  test_list_devices_reports_runtime_contract();
  test_self_test_reports_missing_model_clearly();

  std::cout << "[PASS] test_voice_mic_cli_contract\n";
  return 0;
}
