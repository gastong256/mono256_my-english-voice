#pragma once

#include <atomic>
#include <cstdint>
#include <thread>

#include "mev/audio/i_audio_input.hpp"

namespace mev {

class SimulatedAudioInput final : public IAudioInput {
 public:
  SimulatedAudioInput(std::uint32_t sample_rate_hz, std::uint16_t channels, std::uint32_t frames_per_buffer);
  ~SimulatedAudioInput() override;

  bool start(AudioInputCallback callback) override;
  void stop() override;
  [[nodiscard]] std::string name() const override { return "simulated_input"; }

 private:
  void run(std::stop_token stop_token);

  std::uint32_t sample_rate_hz_;
  std::uint16_t channels_;
  std::uint32_t frames_per_buffer_;
  AudioInputCallback callback_;
  std::jthread thread_;
  std::atomic<bool> running_{false};
};

}  // namespace mev
