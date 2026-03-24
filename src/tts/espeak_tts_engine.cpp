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

bool EspeakTTSEngine::initialize(const TTSConfig& /*config*/, std::string& error) {
  if (initialized_) {
    return true;
  }

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
  warmed_up_ = false;
  runtime_summary_ = "provider=cpu requested_device=cpu effective_device=cpu";
  MEV_LOG_INFO("EspeakTTSEngine initialised, sample_rate=", sample_rate_);
  return true;
#else
  error = "EspeakTTSEngine: espeak-ng not compiled in (MEV_ENABLE_ESPEAK=OFF)";
  runtime_summary_ = "provider=unavailable requested_device=cpu reason=MEV_ENABLE_ESPEAK=OFF";
  MEV_LOG_ERROR(error);
  return false;
#endif
}

void EspeakTTSEngine::warmup() {
  if (!initialized_ || warmed_up_) {
    return;
  }

  std::vector<float> pcm;
  if (!synthesize("warmup", pcm)) {
    MEV_LOG_WARN("EspeakTTSEngine warmup failed");
    return;
  }
  warmed_up_ = true;
  MEV_LOG_INFO("EspeakTTSEngine warmup done (generated ", pcm.size(), " samples)");
}

bool EspeakTTSEngine::synthesize(const std::string& text, std::vector<float>& pcm_out) {
  SpeechChunk chunk;
  chunk.text = text;
  chunk.is_partial = false;
  chunk.is_final = true;
  return synthesize_chunk(chunk, pcm_out);
}

bool EspeakTTSEngine::synthesize_chunk(const SpeechChunk& chunk, std::vector<float>& pcm_out) {
  if (!initialized_) {
    return false;
  }

#if defined(MEV_ENABLE_ESPEAK)
  espeak_SetParameter(espeakRATE, chunk.is_partial ? 220 : 170, 0);
  g_espeak_pcm_out.clear();

  unsigned int identifier = 0;
  const espeak_ERROR err = espeak_Synth(
      chunk.text.c_str(),
      chunk.text.size() + 1U,
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
  if (pcm_out.empty()) {
    MEV_LOG_ERROR("EspeakTTSEngine: synthesize produced empty PCM");
    return false;
  }
  return true;
#else
  (void)chunk;
  pcm_out.clear();
  MEV_LOG_ERROR("EspeakTTSEngine: synthesize requested but espeak-ng is not compiled in");
  return false;
#endif
}

void EspeakTTSEngine::shutdown() {
  if (!initialized_) {
    return;
  }
  initialized_ = false;
  warmed_up_ = false;
#if defined(MEV_ENABLE_ESPEAK)
  espeak_Terminate();
  MEV_LOG_INFO("EspeakTTSEngine: terminated");
#endif
}

}  // namespace mev
