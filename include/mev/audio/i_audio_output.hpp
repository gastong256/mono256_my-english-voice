#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>

namespace mev {

using AudioOutputCallback = std::function<void(float* output, std::size_t frames, std::uint16_t channels,
                                               std::uint32_t sample_rate)>;

class IAudioOutput {
 public:
  virtual ~IAudioOutput() = default;

  virtual bool start(AudioOutputCallback callback) = 0;
  virtual void stop() = 0;
  [[nodiscard]] virtual std::string name() const = 0;
};

}  // namespace mev
