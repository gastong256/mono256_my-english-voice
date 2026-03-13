#include "mev/tts/espeak_tts_engine.hpp"

#include "mev/core/logger.hpp"

#if defined(MEV_ENABLE_ESPEAK)
#include <espeak-ng/speak_lib.h>

// Thread-local output buffer filled by the espeak PCM callback.
thread_local std::vector<float> g_espeak_pcm_out;  // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)

static int espeak_pcm_callback(short* wav, int numsamples, espeak_EVENT* events) {
  if (wav != nullptr && numsamples > 0) {
    g_espeak_pcm_out.reserve(g_espeak_pcm_out.size() +
                              static_cast<std::size_t>(numsamples));
    for (int i = 0; i < numsamples; ++i) {
      g_espeak_pcm_out.push_back(static_cast<float>(wav[i]) / 32768.0F);
    }
  }
  (void)events;
  return 0;  // 0 = continue synthesis
}
#endif

namespace mev {

bool EspeakTTSEngine::initialize(const TTSConfig& /*config*/, std::string& /*error*/) {
#if defined(MEV_ENABLE_ESPEAK)
  const int rate = espeak_Initialize(AUDIO_OUTPUT_SYNCHRONOUS, 0, nullptr,
                                     espeakINITIALIZE_DONT_EXIT);
  if (rate < 0) {
    error = "EspeakTTSEngine: espeak_Initialize failed (rate=" + std::to_string(rate) + ")";
    MEV_LOG_ERROR(error);
    return false;
  }
  sample_rate_ = static_cast<int>(rate);
  espeak_SetSynthCallback(espeak_pcm_callback);
  espeak_SetVoiceByName("en");
  espeak_SetParameter(espeakRATE,   170, 0);   // words per minute
  espeak_SetParameter(espeakVOLUME, 90,  0);

  initialized_ = true;
  MEV_LOG_INFO("EspeakTTSEngine initialised, sample_rate=", sample_rate_);
  return true;
#else
  initialized_ = true;
  MEV_LOG_INFO("EspeakTTSEngine initialised as STUB "
               "(compile with MEV_ENABLE_ESPEAK=ON for real synthesis)");
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
  g_espeak_pcm_out.clear();

  unsigned int identifier = 0;
  const espeak_ERROR err = espeak_Synth(
      text.c_str(),
      text.size() + 1U,
      0,
      POS_CHARACTER,
      0,
      espeakCHARS_UTF8 | espeakSSML_NONE,
      &identifier,
      nullptr);

  if (err != EE_OK) {
    MEV_LOG_ERROR("EspeakTTSEngine: espeak_Synth failed (err=", static_cast<int>(err), ")");
    return false;
  }
  espeak_Synchronize();  // wait for completion

  pcm_out = std::move(g_espeak_pcm_out);
  return true;
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
#if defined(MEV_ENABLE_ESPEAK)
  espeak_Terminate();
  MEV_LOG_INFO("EspeakTTSEngine: terminated");
#endif
}

}  // namespace mev
