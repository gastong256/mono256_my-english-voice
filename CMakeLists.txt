cmake_minimum_required(VERSION 3.24)

project(my_english_voice VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ---------------------------------------------------------------------------
# Build options
# ---------------------------------------------------------------------------
option(MEV_ENABLE_STRICT_WARNINGS "Enable strict compiler warnings (-Wall -Wextra -Werror)" ON)
option(MEV_ENABLE_SANITIZERS      "Enable ASan/UBSan in debug-like builds" OFF)
option(MEV_BUILD_TESTS            "Build unit and integration tests" ON)
option(MEV_BUILD_BENCHMARKS       "Build local micro benchmarks" ON)

# Real backends (activate when the corresponding library is installed)
option(MEV_ENABLE_PORTAUDIO       "Enable PortAudio audio I/O backend" OFF)
option(MEV_ENABLE_WHISPER_CPP     "Enable whisper.cpp ASR backend" OFF)
option(MEV_ENABLE_ONNXRUNTIME     "Enable ONNX Runtime (Piper TTS / Silero VAD)" OFF)
option(MEV_ENABLE_ESPEAK          "Enable eSpeak-ng TTS fallback" OFF)
option(MEV_ENABLE_WEBRTCVAD       "Enable libfvad (WebRTC VAD) backend" OFF)
option(MEV_ENABLE_LIBSAMPLERATE   "Enable libsamplerate for high-quality resampling" OFF)

# Developer options
option(MEV_ENABLE_GPU             "Enable GPU execution paths (CUDA via backends)" OFF)
option(MEV_ENABLE_PROFILING       "Enable pipeline profiling macros (MEV_PROFILE_SCOPE)" OFF)
option(MEV_FETCH_DEPS             "Allow FetchContent dependency downloads (e.g. toml++)" OFF)

# ---------------------------------------------------------------------------
# Optional: fetch toml++ (header-only, needed for future TOML loader)
# ---------------------------------------------------------------------------
if(MEV_FETCH_DEPS)
  include(FetchContent)
  FetchContent_Declare(
    tomlplusplus
    GIT_REPOSITORY https://github.com/marzer/tomlplusplus.git
    GIT_TAG        v3.4.0
  )
  FetchContent_MakeAvailable(tomlplusplus)
  # TODO(v2): link tomlplusplus::tomlplusplus to mev and use it to
  #   load config/pipeline.toml + config/tech_glossary.toml natively.
  #   The current INI parser is a lightweight stand-in.
endif()

include(cmake/CompilerWarnings.cmake)
include(cmake/Sanitizers.cmake)

add_subdirectory(src)
add_subdirectory(apps/voice_mic)

if(MEV_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()

if(MEV_BUILD_BENCHMARKS)
  add_subdirectory(benchmarks)
endif()
