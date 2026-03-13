#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>

namespace mev {

using AudioInputCallback = std::function<void(const float* input, std::size_t frames, std::uint16_t channels,
                                              std::uint32_t sample_rate)>;

class IAudioInput {
 public:
  virtual ~IAudioInput() = default;

  virtual bool start(AudioInputCallback callback) = 0;
  virtual void stop() = 0;
  [[nodiscard]] virtual std::string name() const = 0;
};

}  // namespace mev
