#pragma once

#include <atomic>
#include <cstdint>
#include <string>

#include "mev/audio/i_audio_input.hpp"

#if defined(MEV_ENABLE_PORTAUDIO)
#include <portaudio.h>
#endif

namespace mev {

// ---------------------------------------------------------------------------
// PortAudioInput — IAudioInput backed by PortAudio.
//
// The RT callback stores the AudioInputCallback provided by start() and
// invokes it directly from the PortAudio callback thread. The orchestrator's
// on_audio_input_callback is RT-safe (bounded memcpy + atomic only).
//
// When MEV_ENABLE_PORTAUDIO is OFF, all methods are no-ops / stubs.
// ---------------------------------------------------------------------------
class PortAudioInput final : public IAudioInput {
 public:
  PortAudioInput(std::uint32_t sample_rate, std::uint16_t channels,
                 std::uint32_t frames_per_buffer, std::string device_name);
  ~PortAudioInput() override;

  bool start(AudioInputCallback callback) override;
  void stop() override;
  [[nodiscard]] std::string name() const override;

  // Log all available input devices to stdout.
  static void list_devices();

 private:
#if defined(MEV_ENABLE_PORTAUDIO)
  static int pa_input_callback(const void* input, void* output,
                               unsigned long frame_count,
                               const PaStreamCallbackTimeInfo* time_info,
                               PaStreamCallbackFlags status_flags,
                               void* userdata);
  PaStream* stream_{nullptr};
#endif

  AudioInputCallback callback_;
  std::atomic<std::uint64_t> overrun_count_{0};
  std::uint32_t sample_rate_;
  std::uint32_t frames_per_buffer_;
  std::uint16_t channels_;
  std::string device_name_;
};

}  // namespace mev
