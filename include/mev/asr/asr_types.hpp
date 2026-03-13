#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "mev/core/types.hpp"

namespace mev {

struct AsrRequest {
  SequenceNumber sequence{0};
  TimePoint created_at{};
  std::uint32_t sample_rate{16000};
  std::vector<float> mono_pcm{};
  std::string prompt_hint{};
};

struct AsrPartialHypothesis {
  SequenceNumber sequence{0};
  TimePoint created_at{};
  std::string source_text_es{};
  std::string translated_text_en{};
  float stability{0.0F};
  bool end_of_utterance{false};
};

}  // namespace mev
