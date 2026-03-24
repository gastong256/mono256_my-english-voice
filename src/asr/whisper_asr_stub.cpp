#include "mev/asr/whisper_asr_stub.hpp"

#include <algorithm>
#include <array>

namespace mev {

WhisperAsrStub::WhisperAsrStub(std::string model_path, const bool enable_gpu)
    : model_path_(std::move(model_path)), enable_gpu_(enable_gpu) {}

std::string WhisperAsrStub::runtime_summary() const {
  return std::string("provider=stub requested_device=") +
         (enable_gpu_ ? "gpu" : "cpu") +
         " effective_device=cpu reason=MEV_ENABLE_WHISPER_CPP=OFF";
}

bool WhisperAsrStub::warmup(std::string& error) {
  if (model_path_.empty()) {
    error = "ASR model path is empty";
    return false;
  }
  (void)enable_gpu_;
  return true;
}

AsrPartialHypothesis WhisperAsrStub::transcribe_incremental(const AsrRequest& request) {
  static constexpr std::array<const char*, 8> kEsText = {
      "necesitamos revisar la arquitectura de backend",
      "el servicio de redis tiene alta latencia",
      "vamos a desplegar en kubernetes con terraform",
      "la base de datos postgresql necesita tuning",
      "ci slash cd esta estable en produccion",
      "queremos mejorar observability y tracing",
      "el endpoint requiere rate limiting e idempotency",
      "vamos a optimizar throughput sin romper el slo",
  };

  static constexpr std::array<const char*, 8> kEnText = {
      "we need to review the backend architecture",
      "the redis service is showing high latency",
      "we are deploying on kubernetes with terraform",
      "the postgresql database needs tuning",
      "ci cd is stable in production",
      "we want to improve observability and tracing",
      "the endpoint needs rate limiting and idempotency",
      "we should optimize throughput without breaking the slo",
  };

  const auto idx = static_cast<std::size_t>(counter_.fetch_add(1, std::memory_order_relaxed) % kEnText.size());

  AsrPartialHypothesis out;
  out.sequence = request.sequence;
  out.created_at = Clock::now();
  out.source_text_es = kEsText[idx];
  out.translated_text_en = kEnText[idx];

  const auto pcm_size = request.mono_pcm.size();
  const float stability = std::clamp(0.35F + static_cast<float>((pcm_size % 4096U)) / 4096.0F, 0.0F, 0.95F);
  out.stability = stability;
  out.end_of_utterance = pcm_size > 8000 && (idx % 3 == 0);

  if (!request.prompt_hint.empty() && out.translated_text_en.find("observability") != std::string::npos) {
    out.translated_text_en += " in the current session context";
  }

  return out;
}

}  // namespace mev
