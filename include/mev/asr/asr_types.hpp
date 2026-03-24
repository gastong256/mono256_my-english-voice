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
  bool stream_continues{false};  // true for rolling/overlapping chunks, false for final flush
};

struct AsrPartialHypothesis {
  SequenceNumber sequence{0};
  TimePoint created_at{};
  std::string source_text_es{};
  std::string translated_text_en{};
  std::string raw_translated_text_en{};
  std::string stable_prefix_en{};
  float stability{0.0F};
  std::uint64_t revision{0};
  bool is_partial{true};
  bool end_of_utterance{false};
};

}  // namespace mev
