#include "mev/tts/onnx_tts_stub.hpp"

#include <algorithm>
#include <cmath>

#include "mev/core/logger.hpp"

namespace mev {

OnnxTtsStub::OnnxTtsStub(std::string model_path, std::string /*speaker_reference*/,
                          const bool /*enable_gpu*/)
    : model_path_(std::move(model_path)) {}

bool OnnxTtsStub::initialize(const TTSConfig& config, std::string& /*error*/) {
  model_path_ = config.model_path;
  MEV_LOG_INFO("OnnxTtsStub::initialize (stub, no real ONNX inference)");
  return true;
}

void OnnxTtsStub::warmup() {
  std::vector<float> dummy;
  (void)synthesize("warmup test", dummy);
  MEV_LOG_INFO("OnnxTtsStub warmup done (", dummy.size(), " samples)");
}

bool OnnxTtsStub::synthesize(const std::string& text, std::vector<float>& pcm_out) {
  constexpr float two_pi = 6.28318530718F;

  const auto words = 1U + static_cast<unsigned int>(std::count(text.begin(), text.end(), ' '));
  const auto sample_count = static_cast<std::size_t>(words) * 1800U;
  pcm_out.resize(sample_count, 0.0F);

  auto phase = static_cast<float>(phase_);
  for (std::size_t i = 0; i < pcm_out.size(); ++i) {
    const float envelope = std::exp(-2.5F * static_cast<float>(i) / static_cast<float>(pcm_out.size()));
    pcm_out[i] = 0.10F * envelope * std::sin(phase);
    phase += two_pi * 180.0F / 24000.0F;
    if (phase >= two_pi) {
      phase -= two_pi;
    }
  }
  phase_ = static_cast<double>(phase);

  return true;
}

}  // namespace mev
