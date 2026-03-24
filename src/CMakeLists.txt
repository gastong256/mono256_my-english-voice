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
  if(WIN32)
    # On Windows: caller sets PORTAUDIO_ROOT or installs via vcpkg
    if(DEFINED PORTAUDIO_ROOT)
      target_include_directories(mev PRIVATE "${PORTAUDIO_ROOT}/include")
      target_link_libraries(mev PUBLIC "${PORTAUDIO_ROOT}/lib/portaudio.lib")
    else()
      find_package(portaudio CONFIG REQUIRED)
      target_link_libraries(mev PUBLIC portaudio)
    endif()
  else()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(PORTAUDIO REQUIRED portaudio-2.0)
    target_include_directories(mev PRIVATE ${PORTAUDIO_INCLUDE_DIRS})
    target_link_libraries(mev PUBLIC ${PORTAUDIO_LIBRARIES})
  endif()
  target_compile_definitions(mev PUBLIC MEV_ENABLE_PORTAUDIO=1)
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
  target_include_directories(mev PUBLIC "${ONNXRUNTIME_ROOT}/include")
  if(WIN32)
    target_link_libraries(mev PUBLIC "${ONNXRUNTIME_ROOT}/lib/onnxruntime.lib")
    # Copy the DLL next to the executable at build time.
    add_custom_command(TARGET mev POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${ONNXRUNTIME_ROOT}/lib/onnxruntime.dll"
        "$<TARGET_FILE_DIR:mev>"
      COMMENT "Copying onnxruntime.dll"
    )
  else()
    target_link_libraries(mev PUBLIC "${ONNXRUNTIME_ROOT}/lib/libonnxruntime.so")
  endif()
endif()

if(MEV_ENABLE_ESPEAK)
  if(WIN32)
    if(DEFINED ESPEAK_ROOT)
      target_include_directories(mev PRIVATE "${ESPEAK_ROOT}/include")
      target_link_libraries(mev PUBLIC "${ESPEAK_ROOT}/lib/espeak-ng.lib")
    else()
      find_package(espeak-ng CONFIG REQUIRED)
      target_link_libraries(mev PUBLIC espeak-ng::espeak-ng)
    endif()
  else()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(ESPEAK REQUIRED espeak-ng)
    target_include_directories(mev PRIVATE ${ESPEAK_INCLUDE_DIRS})
    target_link_libraries(mev PUBLIC ${ESPEAK_LIBRARIES})
  endif()
  target_compile_definitions(mev PUBLIC MEV_ENABLE_ESPEAK=1)
endif()

if(MEV_ENABLE_WEBRTCVAD)
  target_compile_definitions(mev PUBLIC MEV_ENABLE_WEBRTCVAD=1)
  target_link_libraries(mev PUBLIC fvad)
endif()

if(MEV_ENABLE_LIBSAMPLERATE)
  if(WIN32)
    if(DEFINED SAMPLERATE_ROOT)
      target_include_directories(mev PRIVATE "${SAMPLERATE_ROOT}/include")
      target_link_libraries(mev PUBLIC "${SAMPLERATE_ROOT}/lib/samplerate.lib")
    else()
      find_package(SampleRate CONFIG REQUIRED)
      target_link_libraries(mev PUBLIC SampleRate::samplerate)
    endif()
  else()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(SAMPLERATE REQUIRED samplerate)
    target_include_directories(mev PRIVATE ${SAMPLERATE_INCLUDE_DIRS})
    target_link_libraries(mev PUBLIC ${SAMPLERATE_LIBRARIES})
  endif()
  target_compile_definitions(mev PUBLIC MEV_ENABLE_LIBSAMPLERATE=1)
endif()

if(MEV_ENABLE_PROFILING)
  target_compile_definitions(mev PUBLIC MEV_ENABLE_PROFILING=1)
endif()

if(MEV_ENABLE_GPU)
  target_compile_definitions(mev PUBLIC MEV_ENABLE_GPU=1)
endif()

mev_set_warnings(mev)
mev_enable_sanitizers(mev)
