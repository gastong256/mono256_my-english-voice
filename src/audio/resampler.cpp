#include "mev/audio/resampler.hpp"

#include <algorithm>
#include <cstring>

#include "mev/core/logger.hpp"

namespace mev {

Resampler::~Resampler() {
#if defined(MEV_ENABLE_LIBSAMPLERATE)
  if (state_ != nullptr) {
    src_delete(state_);
    state_ = nullptr;
  }
#endif
}

bool Resampler::initialize(double ratio, int channels) {
  ratio_    = ratio;
  channels_ = channels;

#if defined(MEV_ENABLE_LIBSAMPLERATE)
  if (state_ != nullptr) {
    src_delete(state_);
    state_ = nullptr;
  }
  int error = 0;
  state_ = src_new(SRC_SINC_MEDIUM_QUALITY, channels_, &error);
  if (state_ == nullptr) {
    MEV_LOG_ERROR("Resampler: src_new failed: ", src_strerror(error));
    return false;
  }
  initialized_ = true;
  MEV_LOG_INFO("Resampler: initialized ratio=", ratio_, " channels=", channels_);
  return true;
#else
  if (ratio_ != 1.0) {
    MEV_LOG_WARN("Resampler: libsamplerate not compiled in; "
                 "ratio=", ratio_, " — output will copy input without conversion");
  }
  initialized_ = true;
  return true;
#endif
}

std::size_t Resampler::process(const float* input, std::size_t input_frames,
                               float* output, std::size_t output_capacity) {
  if (!initialized_ || input_frames == 0) return 0;

#if defined(MEV_ENABLE_LIBSAMPLERATE)
  if (state_ == nullptr) return 0;

  SRC_DATA data{};
  data.data_in       = input;
  data.data_out      = output;
  data.input_frames  = static_cast<long>(input_frames);
  data.output_frames = static_cast<long>(output_capacity);
  data.src_ratio     = ratio_;
  data.end_of_input  = 0;

  const int err = src_process(state_, &data);
  if (err != 0) {
    MEV_LOG_ERROR("Resampler: src_process failed: ", src_strerror(err));
    return 0;
  }
  return static_cast<std::size_t>(data.output_frames_gen);
#else
  // Passthrough: copy up to min(input_frames, output_capacity) frames.
  const std::size_t frames_to_copy = std::min(input_frames, output_capacity);
  std::memcpy(output, input, frames_to_copy * static_cast<std::size_t>(channels_) * sizeof(float));
  return frames_to_copy;
#endif
}

void Resampler::reset() {
#if defined(MEV_ENABLE_LIBSAMPLERATE)
  if (state_ != nullptr) {
    src_reset(state_);
  }
#endif
}

}  // namespace mev
