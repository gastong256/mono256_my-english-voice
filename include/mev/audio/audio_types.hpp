#pragma once

#include <array>
#include <cstdint>

#include "mev/core/types.hpp"

namespace mev {

constexpr std::size_t kMaxFramesPerBlock = 2048;
constexpr std::size_t kMaxAudioChannels = 2;

struct RawAudioBlock {
  SequenceNumber sequence{0};
  TimePoint capture_time{};
  std::uint32_t sample_rate{48000};
  std::uint16_t channels{1};
  std::uint16_t frames{0};
  std::array<float, kMaxFramesPerBlock * kMaxAudioChannels> interleaved{};
};

struct OutputAudioBlock {
  SequenceNumber sequence{0};
  std::uint16_t frames{0};
  std::array<float, kMaxFramesPerBlock> mono{};
};

}  // namespace mev
