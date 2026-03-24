#pragma once

#include <string>
#include <vector>

#include "mev/core/types.hpp"
#include "mev/tts/tts_types.hpp"

namespace mev {

[[nodiscard]] std::vector<SpeechChunk> chunk_text_for_realtime_tts(
    SequenceNumber sequence, const std::string& text, bool is_partial, TimePoint now,
    std::uint32_t chunk_budget_ms);

}  // namespace mev
