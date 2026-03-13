#include "mev/audio/simulated_audio_output.hpp"

#include <chrono>
#include <thread>
#include <vector>

namespace mev {

SimulatedAudioOutput::SimulatedAudioOutput(const std::uint32_t sample_rate_hz, const std::uint16_t channels,
                                           const std::uint32_t frames_per_buffer)
    : sample_rate_hz_(sample_rate_hz), channels_(channels), frames_per_buffer_(frames_per_buffer) {}

SimulatedAudioOutput::~SimulatedAudioOutput() { stop(); }

bool SimulatedAudioOutput::start(AudioOutputCallback callback) {
  if (running_.exchange(true)) {
    return false;
  }

  callback_ = std::move(callback);
  thread_ = std::jthread([this](std::stop_token token) { run(token); });
  return true;
}

void SimulatedAudioOutput::stop() {
  if (!running_.exchange(false)) {
    return;
  }
  if (thread_.joinable()) {
    thread_.request_stop();
    thread_.join();
  }
}

void SimulatedAudioOutput::run(std::stop_token stop_token) {
  std::vector<float> out(static_cast<std::size_t>(frames_per_buffer_) * channels_, 0.0F);

  auto next_tick = std::chrono::steady_clock::now();
  const auto period = std::chrono::microseconds((1000000LL * static_cast<long long>(frames_per_buffer_)) /
                                                 static_cast<long long>(sample_rate_hz_));

  while (!stop_token.stop_requested()) {
    if (callback_) {
      callback_(out.data(), frames_per_buffer_, channels_, sample_rate_hz_);
    }

    next_tick += period;
    std::this_thread::sleep_until(next_tick);
  }
}

}  // namespace mev
