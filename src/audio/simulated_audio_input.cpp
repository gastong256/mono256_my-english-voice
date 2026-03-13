#include "mev/audio/simulated_audio_input.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <thread>
#include <vector>

namespace mev {

SimulatedAudioInput::SimulatedAudioInput(const std::uint32_t sample_rate_hz, const std::uint16_t channels,
                                         const std::uint32_t frames_per_buffer)
    : sample_rate_hz_(sample_rate_hz), channels_(channels), frames_per_buffer_(frames_per_buffer) {}

SimulatedAudioInput::~SimulatedAudioInput() { stop(); }

bool SimulatedAudioInput::start(AudioInputCallback callback) {
  if (running_.exchange(true)) {
    return false;
  }

  callback_ = std::move(callback);
  thread_ = std::jthread([this](std::stop_token token) { run(token); });
  return true;
}

void SimulatedAudioInput::stop() {
  if (!running_.exchange(false)) {
    return;
  }
  if (thread_.joinable()) {
    thread_.request_stop();
    thread_.join();
  }
}

void SimulatedAudioInput::run(std::stop_token stop_token) {
  constexpr float two_pi = 6.28318530718F;
  const float freq = 220.0F;
  float phase = 0.0F;

  std::vector<float> buffer(static_cast<std::size_t>(frames_per_buffer_) * channels_, 0.0F);

  auto next_tick = std::chrono::steady_clock::now();
  const auto period = std::chrono::microseconds((1000000LL * static_cast<long long>(frames_per_buffer_)) /
                                                 static_cast<long long>(sample_rate_hz_));

  while (!stop_token.stop_requested()) {
    for (std::uint32_t frame = 0; frame < frames_per_buffer_; ++frame) {
      const float sample = 0.12F * std::sin(phase);
      phase += two_pi * freq / static_cast<float>(sample_rate_hz_);
      if (phase >= two_pi) {
        phase -= two_pi;
      }

      for (std::uint16_t ch = 0; ch < channels_; ++ch) {
        buffer[static_cast<std::size_t>(frame) * channels_ + ch] = sample;
      }
    }

    if (callback_) {
      callback_(buffer.data(), frames_per_buffer_, channels_, sample_rate_hz_);
    }

    next_tick += period;
    std::this_thread::sleep_until(next_tick);
  }
}

}  // namespace mev
