#include <cassert>
#include <cmath>
#include <iostream>
#include <vector>

#include "mev/audio/webrtc_vad_engine.hpp"
#include "mev/config/app_config.hpp"

int main() {
#if !defined(MEV_ENABLE_WEBRTCVAD)
  std::cout << "[SKIP] test_vad_engine: MEV_ENABLE_WEBRTCVAD=OFF\n";
  return 0;
#else
  mev::WebRtcVadEngine vad;
  mev::VadConfig cfg{};
  cfg.threshold  = 0.5F;

  // Force 16kHz for fvad.
  const bool init_ok = vad.initialize(cfg);
  assert(init_ok && "WebRtcVadEngine::initialize must succeed");
  assert(vad.name() == "webrtc_vad");

  // 30ms frame at 16kHz = 480 samples.
  constexpr std::size_t kFrameSamples = 480;

  // --- Test: silence (all zeros) -------------------------------------------
  std::vector<int16_t> silence(kFrameSamples, 0);
  const float silence_prob = vad.process_frame(silence.data(), silence.size());
  assert(silence_prob == 0.0F && "silence frame must return 0.0");

  // --- Test: 400Hz sine at high amplitude (speech-like) --------------------
  std::vector<int16_t> tone(kFrameSamples);
  for (std::size_t i = 0; i < kFrameSamples; ++i) {
    const double t   = static_cast<double>(i) / 16000.0;
    const double val = std::sin(2.0 * M_PI * 400.0 * t) * 28000.0;
    tone[i] = static_cast<int16_t>(val);
  }
  const float tone_prob = vad.process_frame(tone.data(), tone.size());
  // libfvad mode 2: 400Hz tone at high amplitude should be detected as speech.
  assert(tone_prob > 0.5F && "400Hz tone must be detected as speech");

  // Test reset doesn't crash.
  vad.reset();
  const float after_reset = vad.process_frame(silence.data(), silence.size());
  assert(after_reset == 0.0F && "silence after reset must still return 0.0");

  std::cout << "[PASS] test_vad_engine: silence=" << silence_prob
            << " tone=" << tone_prob << "\n";
  return 0;
#endif
}
