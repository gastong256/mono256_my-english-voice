find_package(Threads REQUIRED)

add_library(mev STATIC
  app/application.cpp
  audio/simulated_audio_input.cpp
  audio/simulated_audio_output.cpp
  asr/whisper_asr_stub.cpp
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

target_link_libraries(mev PUBLIC Threads::Threads)

# ---------------------------------------------------------------------------
# Optional feature flags
# ---------------------------------------------------------------------------

if(MEV_ENABLE_PORTAUDIO)
  target_compile_definitions(mev PUBLIC MEV_ENABLE_PORTAUDIO=1)
  # TODO: find_package(PortAudio REQUIRED) and target_link_libraries(mev PUBLIC portaudio)
endif()

if(MEV_ENABLE_WHISPER_CPP)
  target_compile_definitions(mev PUBLIC MEV_ENABLE_WHISPER_CPP=1)
  # TODO: find_package(whisper REQUIRED) and link
endif()

if(MEV_ENABLE_ONNXRUNTIME)
  target_compile_definitions(mev PUBLIC MEV_ENABLE_ONNXRUNTIME=1)
  # TODO: find_package(onnxruntime REQUIRED) and link
endif()

if(MEV_ENABLE_ESPEAK)
  target_compile_definitions(mev PUBLIC MEV_ENABLE_ESPEAK=1)
  # TODO: target_link_libraries(mev PUBLIC espeak-ng)
endif()

if(MEV_ENABLE_WEBRTCVAD)
  target_compile_definitions(mev PUBLIC MEV_ENABLE_WEBRTCVAD=1)
  # TODO: target_link_libraries(mev PUBLIC fvad)
endif()

if(MEV_ENABLE_LIBSAMPLERATE)
  target_compile_definitions(mev PUBLIC MEV_ENABLE_LIBSAMPLERATE=1)
  # TODO: find_package(SampleRate REQUIRED) and link
  # Used in: ingest_loop (resample to 16kHz) and tts_loop (resample output)
endif()

if(MEV_ENABLE_PROFILING)
  target_compile_definitions(mev PUBLIC MEV_ENABLE_PROFILING=1)
endif()

if(MEV_ENABLE_GPU)
  target_compile_definitions(mev PUBLIC MEV_ENABLE_GPU=1)
endif()

mev_set_warnings(mev)
mev_enable_sanitizers(mev)
