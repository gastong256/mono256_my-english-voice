#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "mev/audio/i_vad_engine.hpp"

namespace mev {

// ---------------------------------------------------------------------------
// NullVadEngine — Fallback VAD that always reports voice active (1.0).
//
// Used when no real VAD backend is compiled in. Results in fixed-window
// chunking behavior (same as legacy).
// ---------------------------------------------------------------------------
class NullVadEngine final : public IVadEngine {
 public:
  bool initialize(const VadConfig& /*config*/) override { return true; }

  float process_frame(const int16_t* /*samples*/, std::size_t /*num_samples*/) override {
    return 1.0F;
  }

  void reset() override {}

  [[nodiscard]] std::string name() const override { return "null_vad"; }
};

}  // namespace mev
