#pragma once

#include <cstddef>

#if defined(MEV_ENABLE_LIBSAMPLERATE)
#include <samplerate.h>
#endif

namespace mev {

// ---------------------------------------------------------------------------
// Resampler — high-quality sample rate conversion via libsamplerate.
//
// When MEV_ENABLE_LIBSAMPLERATE is ON, uses SRC_SINC_MEDIUM_QUALITY.
// When OFF, copies input directly to output (assumes matching rates) and
// logs a warning if ratio != 1.0.
// ---------------------------------------------------------------------------
class Resampler {
 public:
  Resampler() = default;
  ~Resampler();

  // Initialize with ratio = output_sample_rate / input_sample_rate.
  bool initialize(double ratio, int channels = 1);

  // Resample input_frames frames from input to output.
  // output capacity must be >= std::ceil(input_frames * ratio) + 16.
  // Returns number of frames written to output.
  [[nodiscard]] std::size_t process(const float* input, std::size_t input_frames,
                                    float* output, std::size_t output_capacity);

  // Reset internal state (e.g., between utterances).
  void reset();

  [[nodiscard]] double ratio() const { return ratio_; }

 private:
#if defined(MEV_ENABLE_LIBSAMPLERATE)
  SRC_STATE* state_{nullptr};
#endif
  double ratio_{1.0};
  int channels_{1};
  bool initialized_{false};
};

}  // namespace mev
