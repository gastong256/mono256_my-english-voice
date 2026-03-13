#pragma once

#include <atomic>
#include <cstdint>
#include <string>

#include "mev/audio/i_audio_output.hpp"

#if defined(MEV_ENABLE_PORTAUDIO)
#include <portaudio.h>
#endif

namespace mev {

// ---------------------------------------------------------------------------
// PortAudioOutput — IAudioOutput backed by PortAudio.
//
// The RT callback invokes the AudioOutputCallback provided via start().
// Silence is handled by the orchestrator's on_audio_output_callback.
//
// When MEV_ENABLE_PORTAUDIO is OFF, all methods are no-ops / stubs.
// ---------------------------------------------------------------------------
class PortAudioOutput final : public IAudioOutput {
 public:
  PortAudioOutput(std::uint32_t sample_rate, std::uint16_t channels,
                  std::uint32_t frames_per_buffer, std::string device_name);
  ~PortAudioOutput() override;

  bool start(AudioOutputCallback callback) override;
  void stop() override;
  [[nodiscard]] std::string name() const override;

  // Log all available output devices to stdout.
  static void list_devices();

 private:
#if defined(MEV_ENABLE_PORTAUDIO)
  static int pa_output_callback(const void* input, void* output,
                                unsigned long frame_count,
                                const PaStreamCallbackTimeInfo* time_info,
                                PaStreamCallbackFlags status_flags,
                                void* userdata);
  PaStream* stream_{nullptr};
#endif

  AudioOutputCallback callback_;
  std::atomic<std::uint64_t> underrun_count_{0};
  std::uint32_t sample_rate_;
  std::uint32_t frames_per_buffer_;
  std::uint16_t channels_;
  std::string device_name_;
};

}  // namespace mev
