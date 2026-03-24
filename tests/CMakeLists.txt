function(add_mev_test name)
  add_executable(${name} ${ARGN})
  target_link_libraries(${name} PRIVATE mev)
  mev_set_warnings(${name})
  mev_enable_sanitizers(${name})
  add_test(NAME ${name} COMMAND ${name})
endfunction()

# Existing tests
add_mev_test(test_spsc_ring        test_spsc_ring.cpp)
add_mev_test(test_chunk_committer  test_chunk_committer.cpp)
add_mev_test(test_domain_adapter   test_domain_adapter.cpp)
add_mev_test(test_pipeline_wiring  test_pipeline_wiring.cpp)

# New tests added in refactor
add_mev_test(test_utterance_lifecycle  test_utterance_lifecycle.cpp)
add_mev_test(test_tts_scheduler_policy test_tts_scheduler_policy.cpp)
add_mev_test(test_domain_corrections   test_domain_corrections.cpp)
add_mev_test(test_gpu_scheduler        test_gpu_scheduler.cpp)
add_mev_test(test_pipeline_shutdown    test_pipeline_shutdown.cpp)
add_mev_test(test_runtime_backend_selection test_runtime_backend_selection.cpp)
add_mev_test(test_voice_mic_cli_contract test_voice_mic_cli_contract.cpp)
add_mev_test(test_asr_incremental_contract test_asr_incremental_contract.cpp)

# Always compiled: config parser and public CLI contract tests
add_mev_test(test_toml_config test_toml_config.cpp)

# Conditionally compiled: real backend tests (skip gracefully when libs absent)
if(MEV_ENABLE_LIBSAMPLERATE)
  add_mev_test(test_resampler test_resampler.cpp)
endif()

if(MEV_ENABLE_WHISPER_CPP)
  add_mev_test(test_whisper_asr test_whisper_asr.cpp)
endif()

if(MEV_ENABLE_WEBRTCVAD)
  add_mev_test(test_vad_engine test_vad_engine.cpp)
endif()

if(MEV_ENABLE_PORTAUDIO)
  add_mev_test(test_portaudio_enum test_portaudio_enum.cpp)
endif()

if(MEV_ENABLE_ESPEAK)
  add_mev_test(test_espeak_tts test_espeak_tts.cpp)
  add_mev_test(test_tts_fallback_contract test_tts_fallback_contract.cpp)
endif()

if(MEV_ENABLE_ONNXRUNTIME)
  add_mev_test(test_piper_tts test_piper_tts.cpp)
endif()
