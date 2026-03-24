#include "mev/asr/whisper_asr_engine.hpp"

#include "mev/core/logger.hpp"
#include "mev/core/time.hpp"

namespace mev {

WhisperASREngine::WhisperASREngine(const std::string& model_path, bool enable_gpu,
                                   std::string language, bool translate,
                                   std::string quantization)
    : model_path_(model_path),
      enable_gpu_(enable_gpu),
      language_(std::move(language)),
      translate_(translate),
      quantization_(std::move(quantization)) {}

WhisperASREngine::~WhisperASREngine() {
#if defined(MEV_ENABLE_WHISPER_CPP)
  if (ctx_ != nullptr) {
    whisper_free(ctx_);
    ctx_ = nullptr;
  }
#endif
}

bool WhisperASREngine::warmup(std::string& error) {
#if defined(MEV_ENABLE_WHISPER_CPP)
  if (model_path_.empty()) {
    error = "WhisperASREngine: model path is empty";
    MEV_LOG_ERROR(error);
    return false;
  }

  if (ctx_ != nullptr) {
    whisper_free(ctx_);
    ctx_ = nullptr;
  }

  whisper_context_params cparams = whisper_context_default_params();
  cparams.use_gpu = enable_gpu_;

  ctx_ = whisper_init_from_file_with_params(model_path_.c_str(), cparams);
  if (ctx_ == nullptr) {
    error = "WhisperASREngine: failed to load model from '" + model_path_ + "'";
    MEV_LOG_ERROR(error);
    return false;
  }

  MEV_LOG_INFO("WhisperASREngine: model loaded from '", model_path_,
               "' gpu=", enable_gpu_,
               " language=", language_,
               " translate=", translate_,
               " quantization=", quantization_);
  return true;
#else
  MEV_LOG_WARN("WhisperASREngine: whisper.cpp not compiled (MEV_ENABLE_WHISPER_CPP=OFF)");
  error = "whisper.cpp not compiled in";
  return false;
#endif
}

AsrPartialHypothesis WhisperASREngine::transcribe_incremental(const AsrRequest& request) {
  AsrPartialHypothesis result{};
  result.sequence        = request.sequence;
  result.created_at      = Clock::now();
  result.stability       = 0.0F;
  result.end_of_utterance = true;

#if defined(MEV_ENABLE_WHISPER_CPP)
  if (ctx_ == nullptr) {
    MEV_LOG_ERROR("WhisperASREngine: not initialized");
    return result;
  }

  const float* pcm      = request.mono_pcm.data();
  const int    n_samples = static_cast<int>(request.mono_pcm.size());
  if (n_samples == 0) return result;

  whisper_full_params params = whisper_full_default_params(WHISPER_SAMPLING_GREEDY);
  params.language          = language_.c_str();
  params.translate         = translate_;
  params.no_timestamps     = true;
  params.single_segment    = true;
  params.print_progress    = false;
  params.print_realtime    = false;
  params.print_special     = false;

  if (!domain_prompt_.empty()) {
    params.initial_prompt = domain_prompt_.c_str();
  }
  if (!prev_tokens_.empty()) {
    params.prompt_tokens   = prev_tokens_.data();
    params.prompt_n_tokens = static_cast<int>(prev_tokens_.size());
  }

  const int rc = whisper_full(ctx_, params, pcm, n_samples);
  if (rc != 0) {
    MEV_LOG_ERROR("WhisperASREngine: whisper_full() returned ", rc);
    return result;
  }

  // Collect text from all segments.
  std::string text;
  const int n_segments = whisper_full_n_segments(ctx_);
  for (int i = 0; i < n_segments; ++i) {
    const char* seg_text = whisper_full_get_segment_text(ctx_, i);
    if (seg_text != nullptr) {
      text += seg_text;
    }
  }

  // Save context tokens for next call (sliding window).
  prev_tokens_.clear();
  const int n_tokens = whisper_full_n_tokens(ctx_, 0);
  prev_tokens_.reserve(static_cast<std::size_t>(n_tokens));
  for (int i = 0; i < n_tokens; ++i) {
    prev_tokens_.push_back(whisper_full_get_token_id(ctx_, 0, i));
  }

  result.translated_text_en = std::move(text);
  result.stability          = 0.95F;
  result.end_of_utterance   = true;

  inference_count_.fetch_add(1, std::memory_order_relaxed);
  return result;
#else
  MEV_LOG_WARN("WhisperASREngine: whisper.cpp not compiled in");
  return result;
#endif
}

void WhisperASREngine::set_domain_prompt(const std::string& prompt) {
  domain_prompt_ = prompt;
}

void WhisperASREngine::reset_context() {
#if defined(MEV_ENABLE_WHISPER_CPP)
  prev_tokens_.clear();
#endif
}

}  // namespace mev
