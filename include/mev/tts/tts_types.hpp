#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "mev/core/types.hpp"

namespace mev {

struct TtsRequest {
  SequenceNumber sequence{0};
  TimePoint enqueued_at{};
  TimePoint source_started_at{};
  std::string text{};
};

struct TtsResult {
  SequenceNumber sequence{0};
  TimePoint generated_at{};
  std::uint32_t sample_rate{24000};
  std::vector<float> mono_pcm{};
};

}  // namespace mev
