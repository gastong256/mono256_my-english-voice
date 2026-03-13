#include <cassert>
#include <cmath>
#include <iostream>
#include <vector>

#include "mev/audio/resampler.hpp"

int main() {
#if !defined(MEV_ENABLE_LIBSAMPLERATE)
  std::cout << "[SKIP] test_resampler: MEV_ENABLE_LIBSAMPLERATE=OFF\n";
  return 0;
#else
  // Generate 100ms sine at 48kHz (4800 samples).
  constexpr std::uint32_t kInputRate   = 48000;
  constexpr std::uint32_t kOutputRate  = 16000;
  constexpr std::size_t   kInputFrames = kInputRate / 10;  // 100ms

  std::vector<float> input(kInputFrames);
  for (std::size_t i = 0; i < kInputFrames; ++i) {
    const double t = static_cast<double>(i) / static_cast<double>(kInputRate);
    input[i] = static_cast<float>(std::sin(2.0 * M_PI * 400.0 * t));
  }

  mev::Resampler resampler;
  const double ratio = static_cast<double>(kOutputRate) / static_cast<double>(kInputRate);
  assert(resampler.initialize(ratio, 1) && "Resampler::initialize must succeed");
  assert(std::fabs(resampler.ratio() - ratio) < 1e-9 && "ratio accessor must match");

  const std::size_t out_cap = static_cast<std::size_t>(kInputFrames * ratio) + 64;
  std::vector<float> output(out_cap, 0.0F);

  const std::size_t n = resampler.process(
      input.data(), input.size(),
      output.data(), output.size());

  // ~1600 samples expected; allow 1% tolerance.
  const std::size_t expected = kOutputRate / 10;  // 1600
  const double tolerance = static_cast<double>(expected) * 0.01;
  assert(std::fabs(static_cast<double>(n) - static_cast<double>(expected)) <= tolerance &&
         "output frame count must be within 1% of expected");
  assert(n > 0 && "must produce some output");

  // Output must be non-silent (sine wave).
  float max_amp = 0.0F;
  for (std::size_t i = 0; i < n; ++i) {
    if (std::fabs(output[i]) > max_amp) max_amp = std::fabs(output[i]);
  }
  assert(max_amp > 0.1F && "resampled sine must be non-silent");

  // Test reset clears state without crash.
  resampler.reset();
  const std::size_t n2 = resampler.process(
      input.data(), input.size(),
      output.data(), output.size());
  assert(n2 > 0 && "process after reset must succeed");

  std::cout << "[PASS] test_resampler: input=" << kInputFrames
            << " output=" << n << " (expected~" << expected << ")"
            << " max_amp=" << max_amp << "\n";
  return 0;
#endif
}
