#include "mev/tts/stub_tts_engine.hpp"

#include <cmath>
#include <sstream>

#include "mev/core/logger.hpp"

namespace mev {

namespace {
// Count whitespace-delimited tokens as a rough word count.
std::size_t count_words(const std::string& text) {
  std::istringstream ss(text);
  std::string word;
  std::size_t count = 0;
  while (ss >> word) {
    ++count;
  }
  return std::max<std::size_t>(count, 1U);
}
}  // namespace

bool StubTTSEngine::initialize(const TTSConfig& config, std::string& /*error*/) {
  sample_rate_ = static_cast<int>(config.output_sample_rate);
  initialized_ = true;
  MEV_LOG_INFO("StubTTSEngine initialised at ", sample_rate_, " Hz");
  return true;
}

void StubTTSEngine::warmup() {
  std::vector<float> dummy;
  (void)synthesize("warmup", dummy);
  MEV_LOG_INFO("StubTTSEngine warmup done (generated ", dummy.size(), " samples)");
}

bool StubTTSEngine::synthesize(const std::string& text, std::vector<float>& pcm_out) {
  // ~1800 samples per word at configured sample rate — similar to OnnxTtsStub.
  const std::size_t words = count_words(text);
  const std::size_t num_samples = words * 1800U;

  pcm_out.resize(num_samples);

  constexpr double kFreqHz = 180.0;
  const double phase_inc = 2.0 * 3.14159265358979323846 * kFreqHz / static_cast<double>(sample_rate_);

  // Exponential envelope: fast attack, slow decay.
  const double duration = static_cast<double>(num_samples);
  for (std::size_t i = 0; i < num_samples; ++i) {
    const double envelope = std::exp(-3.0 * static_cast<double>(i) / duration);
    pcm_out[i] = static_cast<float>(0.3 * envelope * std::sin(phase_));
    phase_ += phase_inc;
  }

  return true;
}

void StubTTSEngine::shutdown() {
  initialized_ = false;
}

}  // namespace mev
