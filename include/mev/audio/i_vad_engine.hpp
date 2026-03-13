#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "mev/config/app_config.hpp"

namespace mev {

// ---------------------------------------------------------------------------
// IVadEngine — Voice Activity Detection interface.
//
// Process audio frame-by-frame; returns speech probability [0.0, 1.0].
// Frame duration must be exactly 10ms, 20ms, or 30ms at the configured rate.
// Input is int16_t PCM (convert float input before calling).
// ---------------------------------------------------------------------------
class IVadEngine {
 public:
  virtual ~IVadEngine() = default;

  virtual bool initialize(const VadConfig& config) = 0;

  // Process one frame. Returns speech probability in [0.0, 1.0].
  // Frame must be exactly 10ms, 20ms, or 30ms at the configured sample rate.
  virtual float process_frame(const int16_t* samples, std::size_t num_samples) = 0;

  virtual void reset() = 0;

  [[nodiscard]] virtual std::string name() const = 0;
};

}  // namespace mev
