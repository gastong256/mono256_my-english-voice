#pragma once

#include <atomic>
#include <cstdint>
#include <thread>

#include "mev/audio/i_audio_output.hpp"

namespace mev {

class SimulatedAudioOutput final : public IAudioOutput {
 public:
  SimulatedAudioOutput(std::uint32_t sample_rate_hz, std::uint16_t channels, std::uint32_t frames_per_buffer);
  ~SimulatedAudioOutput() override;

  bool start(AudioOutputCallback callback) override;
  void stop() override;
  [[nodiscard]] std::string name() const override { return "simulated_output"; }

 private:
  void run(std::stop_token stop_token);

  std::uint32_t sample_rate_hz_;
  std::uint16_t channels_;
  std::uint32_t frames_per_buffer_;
  AudioOutputCallback callback_;
  std::jthread thread_;
  std::atomic<bool> running_{false};
};

}  // namespace mev
