#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "mev/audio/i_vad_engine.hpp"

#if defined(MEV_ENABLE_WEBRTCVAD)
// Forward declare fvad handle to avoid including fvad.h in the public header.
struct Fvad;
#endif

namespace mev {

// ---------------------------------------------------------------------------
// WebRtcVadEngine — libfvad (dpirch/libfvad) VAD backend.
//
// Supported sample rates: 8000, 16000, 32000, 48000 Hz.
// Supported frame sizes: 10ms, 20ms, or 30ms.
// fvad_process returns 0 (not speech) or 1 (speech) or -1 (error).
// Maps: 1 -> 1.0f, 0 -> 0.0f, -1 -> 0.0f (treat error as silence).
//
// When MEV_ENABLE_WEBRTCVAD is OFF, this compiles as a stub that always
// returns 0.0f and logs a warning.
// ---------------------------------------------------------------------------
class WebRtcVadEngine final : public IVadEngine {
 public:
  WebRtcVadEngine();
  ~WebRtcVadEngine() override;

  bool initialize(const VadConfig& config) override;
  float process_frame(const int16_t* samples, std::size_t num_samples) override;
  void reset() override;
  [[nodiscard]] std::string name() const override { return "webrtc_vad"; }

 private:
#if defined(MEV_ENABLE_WEBRTCVAD)
  Fvad* handle_{nullptr};
#endif
  std::uint32_t sample_rate_{16000};
  bool initialized_{false};
};

}  // namespace mev
