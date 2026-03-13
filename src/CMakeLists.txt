find_package(Threads REQUIRED)

add_library(mev STATIC
  app/application.cpp
  audio/simulated_audio_input.cpp
  audio/simulated_audio_output.cpp
  audio/portaudio_input.cpp
  audio/portaudio_output.cpp
  audio/resampler.cpp
  audio/webrtc_vad_engine.cpp
  asr/whisper_asr_stub.cpp
  asr/whisper_asr_engine.cpp
  config/app_config.cpp
  domain/domain_context_manager.cpp
  domain/technical_domain_adapter.cpp
  infra/metrics.cpp
  pipeline/chunk_committer.cpp
  pipeline/pipeline_orchestrator.cpp
  pipeline/tts_scheduler.cpp
  tts/onnx_tts_stub.cpp
  tts/stub_tts_engine.cpp
  tts/piper_tts_engine.cpp
  tts/espeak_tts_engine.cpp
)

target_include_directories(mev
  PUBLIC
    ${PROJECT_SOURCE_DIR}/include
)

target_link_libraries(mev PUBLIC Threads::Threads tomlplusplus::tomlplusplus)

# ---------------------------------------------------------------------------
# Optional feature flags
# ---------------------------------------------------------------------------

if(MEV_ENABLE_PORTAUDIO)
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(PORTAUDIO REQUIRED portaudio-2.0)
  target_compile_definitions(mev PUBLIC MEV_ENABLE_PORTAUDIO=1)
  target_include_directories(mev PRIVATE ${PORTAUDIO_INCLUDE_DIRS})
  target_link_libraries(mev PUBLIC ${PORTAUDIO_LIBRARIES})
endif()

if(MEV_ENABLE_WHISPER_CPP)
  target_compile_definitions(mev PUBLIC MEV_ENABLE_WHISPER_CPP=1)
  target_link_libraries(mev PUBLIC whisper)
endif()

if(MEV_ENABLE_ONNXRUNTIME)
  if(NOT DEFINED ONNXRUNTIME_ROOT)
    message(FATAL_ERROR "ONNXRUNTIME_ROOT must be set when MEV_ENABLE_ONNXRUNTIME=ON")
  endif()
  target_compile_definitions(mev PUBLIC MEV_ENABLE_ONNXRUNTIME=1)
  target_include_directories(mev PRIVATE "${ONNXRUNTIME_ROOT}/include")
  target_link_libraries(mev PUBLIC "${ONNXRUNTIME_ROOT}/lib/libonnxruntime.so")
endif()

if(MEV_ENABLE_ESPEAK)
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(ESPEAK REQUIRED espeak-ng)
  target_compile_definitions(mev PUBLIC MEV_ENABLE_ESPEAK=1)
  target_include_directories(mev PRIVATE ${ESPEAK_INCLUDE_DIRS})
  target_link_libraries(mev PUBLIC ${ESPEAK_LIBRARIES})
endif()

if(MEV_ENABLE_WEBRTCVAD)
  target_compile_definitions(mev PUBLIC MEV_ENABLE_WEBRTCVAD=1)
  target_link_libraries(mev PUBLIC fvad)
endif()

if(MEV_ENABLE_LIBSAMPLERATE)
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(SAMPLERATE REQUIRED samplerate)
  target_compile_definitions(mev PUBLIC MEV_ENABLE_LIBSAMPLERATE=1)
  target_include_directories(mev PRIVATE ${SAMPLERATE_INCLUDE_DIRS})
  target_link_libraries(mev PUBLIC ${SAMPLERATE_LIBRARIES})
endif()

if(MEV_ENABLE_PROFILING)
  target_compile_definitions(mev PUBLIC MEV_ENABLE_PROFILING=1)
endif()

if(MEV_ENABLE_GPU)
  target_compile_definitions(mev PUBLIC MEV_ENABLE_GPU=1)
endif()

mev_set_warnings(mev)
mev_enable_sanitizers(mev)
