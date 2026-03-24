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
set(MEV_TOMLPLUSPLUS_SOURCE_DIR "" CACHE PATH "Path to a local toml++ checkout")
set(MEV_WHISPER_SOURCE_DIR "" CACHE PATH "Path to a local whisper.cpp checkout")
set(MEV_LIBFVAD_SOURCE_DIR "" CACHE PATH "Path to a local libfvad checkout")

# ---------------------------------------------------------------------------
# Dependency bootstrap
# ---------------------------------------------------------------------------
include(cmake/MevDependencies.cmake)
mev_enable_tomlplusplus()

# ---------------------------------------------------------------------------
# Optional: enable whisper.cpp ASR backend
# ---------------------------------------------------------------------------
if(MEV_ENABLE_WHISPER_CPP)
  mev_enable_whispercpp()
endif()

# ---------------------------------------------------------------------------
# Optional: enable libfvad (WebRTC VAD)
# ---------------------------------------------------------------------------
if(MEV_ENABLE_WEBRTCVAD)
  mev_enable_libfvad()
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
