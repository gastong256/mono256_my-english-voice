#include "mev/tts/espeak_tts_engine.hpp"

#include "mev/core/logger.hpp"

// TODO(espeak): #include <espeak-ng/speak_lib.h>

namespace mev {

bool EspeakTTSEngine::initialize(const TTSConfig& /*config*/, std::string& /*error*/) {
#if defined(MEV_ENABLE_ESPEAK)
  // Real init:
  //   espeak_Initialize(AUDIO_OUTPUT_SYNCHRONOUS, 0, nullptr, 0);
  //   espeak_SetVoiceByName("en");
  //   espeak_SetParameter(espeakRATE, 160, 0);
  MEV_LOG_ERROR("EspeakTTSEngine: espeak-ng not compiled in (MEV_ENABLE_ESPEAK=OFF)");
  return false;
#else
  initialized_ = true;
  MEV_LOG_INFO("EspeakTTSEngine initialised as STUB (compile with MEV_ENABLE_ESPEAK=ON for real synthesis)");
  return true;
#endif
}

void EspeakTTSEngine::warmup() {
  // eSpeak has near-zero cold start — no warmup needed.
  MEV_LOG_INFO("EspeakTTSEngine warmup (no-op)");
}

bool EspeakTTSEngine::synthesize(const std::string& text, std::vector<float>& pcm_out) {
  if (!initialized_) {
    return false;
  }

#if defined(MEV_ENABLE_ESPEAK)
  // Real synthesis path:
  //   espeak_TextToPhonemes() or direct espeak_Synth()
  //   Collect int16 PCM from callback → convert to float32
  (void)text;
  pcm_out.clear();
  return false;
#else
  // Stub: silence proportional to text length.
  const std::size_t approx_samples = (text.size() / 5U + 1U) * (16000U / 10U);
  pcm_out.assign(approx_samples, 0.0F);
  return true;
#endif
}

void EspeakTTSEngine::shutdown() {
  if (!initialized_) {
    return;
  }
  initialized_ = false;
  // TODO(espeak): espeak_Terminate();
}

}  // namespace mev
