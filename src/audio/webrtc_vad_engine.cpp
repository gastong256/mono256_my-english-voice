#include "mev/audio/webrtc_vad_engine.hpp"

#include "mev/core/logger.hpp"

#if defined(MEV_ENABLE_WEBRTCVAD)
#include <fvad.h>
#endif

namespace mev {

WebRtcVadEngine::WebRtcVadEngine() = default;

WebRtcVadEngine::~WebRtcVadEngine() {
#if defined(MEV_ENABLE_WEBRTCVAD)
  if (handle_ != nullptr) {
    fvad_free(handle_);
    handle_ = nullptr;
  }
#endif
}

bool WebRtcVadEngine::initialize(const VadConfig& config) {
  // Default to 16kHz — the ingest loop resamples to 16kHz before VAD.
  // sample_rate_ can be updated before first call to process_frame if needed.
  sample_rate_ = (sample_rate_ == 0) ? 16000U : sample_rate_;

#if defined(MEV_ENABLE_WEBRTCVAD)
  if (handle_ != nullptr) {
    fvad_free(handle_);
    handle_ = nullptr;
  }

  handle_ = fvad_new();
  if (handle_ == nullptr) {
    MEV_LOG_ERROR("WebRtcVadEngine: fvad_new() failed");
    return false;
  }

  // fvad supports: 8000, 16000, 32000, 48000 Hz.
  if (fvad_set_sample_rate(handle_, static_cast<int>(sample_rate_)) != 0) {
    MEV_LOG_ERROR("WebRtcVadEngine: fvad_set_sample_rate(", sample_rate_, ") failed");
    fvad_free(handle_);
    handle_ = nullptr;
    return false;
  }

  // Mode 0 = quality, 3 = very aggressive. Use mode 2 (aggressive) as default.
  fvad_set_mode(handle_, 2);

  initialized_ = true;
  MEV_LOG_INFO("WebRtcVadEngine: initialized sr=", sample_rate_,
               " threshold=", config.threshold);
  return true;
#else
  (void)config;
  MEV_LOG_WARN("WebRtcVadEngine: libfvad not compiled (MEV_ENABLE_WEBRTCVAD=OFF)");
  initialized_ = true;  // stub always initializes
  return true;
#endif
}

float WebRtcVadEngine::process_frame(const int16_t* samples, std::size_t num_samples) {
  if (!initialized_) return 0.0F;

#if defined(MEV_ENABLE_WEBRTCVAD)
  if (handle_ == nullptr) return 0.0F;
  const int result = fvad_process(handle_, samples, num_samples);
  if (result == 1) return 1.0F;
  return 0.0F;  // 0 = not speech, -1 = error
#else
  (void)samples;
  (void)num_samples;
  return 0.0F;
#endif
}

void WebRtcVadEngine::reset() {
#if defined(MEV_ENABLE_WEBRTCVAD)
  if (handle_ != nullptr) {
    fvad_reset(handle_);
  }
#endif
}

}  // namespace mev
