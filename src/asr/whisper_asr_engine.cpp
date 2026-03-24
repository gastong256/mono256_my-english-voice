#include "mev/asr/whisper_asr_engine.hpp"

#include <algorithm>
#include <cctype>
#include <vector>

#include "mev/core/logger.hpp"
#include "mev/core/time.hpp"

namespace mev {

#if defined(MEV_ENABLE_WHISPER_CPP)

namespace {

std::string normalize_whitespace(const std::string& text) {
  std::string out;
  out.reserve(text.size());

  bool in_space = true;
  for (const unsigned char ch : text) {
    if (std::isspace(ch) != 0) {
      if (!in_space) {
        out.push_back(' ');
        in_space = true;
      }
      continue;
    }
    out.push_back(static_cast<char>(ch));
    in_space = false;
  }

  if (!out.empty() && out.back() == ' ') {
    out.pop_back();
  }
  return out;
}

std::vector<std::string> split_tokens(const std::string& text) {
  std::vector<std::string> tokens;
  std::size_t start = 0;
  while (start < text.size()) {
    const auto end = text.find(' ', start);
    if (end == std::string::npos) {
      tokens.push_back(text.substr(start));
      break;
    }
    tokens.push_back(text.substr(start, end - start));
    start = end + 1;
  }
  return tokens;
}

std::string join_tokens(const std::vector<std::string>& tokens, const std::size_t begin,
                        const std::size_t end) {
  if (begin >= end || begin >= tokens.size() || end > tokens.size()) {
    return {};
  }

  std::string joined;
  for (std::size_t i = begin; i < end; ++i) {
    if (!joined.empty()) joined.push_back(' ');
    joined += tokens[i];
  }
  return joined;
}

std::size_t suffix_prefix_overlap(const std::vector<std::string>& left,
                                  const std::vector<std::string>& right) {
  const auto max_overlap = std::min(left.size(), right.size());
  for (std::size_t k = max_overlap; k > 0; --k) {
    if (std::equal(left.end() - static_cast<std::ptrdiff_t>(k), left.end(), right.begin())) {
      return k;
    }
  }
  return 0;
}

float overlap_stability(const std::size_t overlap, const std::size_t denom) {
  if (denom == 0) return 0.0F;
  return std::clamp(static_cast<float>(overlap) / static_cast<float>(denom), 0.0F, 0.95F);
}

}  // namespace

#endif

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
    runtime_summary_ = "provider=unavailable requested_device=" +
                       std::string(enable_gpu_ ? "gpu" : "cpu") +
                       " reason=model path is empty";
    MEV_LOG_ERROR(error);
    return false;
  }

  if (warmed_up_ && ctx_ != nullptr) {
    MEV_LOG_INFO("WhisperASREngine: reusing warmed context (", runtime_summary_, ")");
    return true;
  }

  whisper_context_params cparams = whisper_context_default_params();
  cparams.use_gpu = enable_gpu_;

  ctx_ = whisper_init_from_file_with_params(model_path_.c_str(), cparams);
  if (ctx_ == nullptr) {
    error = "WhisperASREngine: failed to load model from '" + model_path_ + "'";
    gpu_active_ = false;
    runtime_summary_ = "provider=unavailable requested_device=" +
                       std::string(enable_gpu_ ? "gpu" : "cpu") +
                       " reason=failed to load model";
    MEV_LOG_ERROR(error);
    return false;
  }

#if defined(MEV_ENABLE_GPU)
  gpu_active_ = enable_gpu_;
#else
  gpu_active_ = false;
#endif

  runtime_summary_ = std::string("provider=") + (gpu_active_ ? "cuda" : "cpu") +
                     " requested_device=" + (enable_gpu_ ? "gpu" : "cpu");
  if (enable_gpu_ && !gpu_active_) {
    runtime_summary_ += " reason=MEV_ENABLE_GPU=OFF";
  }
  warmed_up_ = true;

  MEV_LOG_INFO("WhisperASREngine: model loaded from '", model_path_,
               "' gpu=", enable_gpu_,
               " language=", language_,
               " translate=", translate_,
               " quantization=", quantization_,
               " ", runtime_summary_);
  return true;
#else
  MEV_LOG_WARN("WhisperASREngine: whisper.cpp not compiled (MEV_ENABLE_WHISPER_CPP=OFF)");
  error = "whisper.cpp not compiled in";
  runtime_summary_ = "provider=stub requested_device=" +
                     std::string(enable_gpu_ ? "gpu" : "cpu") +
                     " reason=MEV_ENABLE_WHISPER_CPP=OFF";
  return false;
#endif
}

AsrPartialHypothesis WhisperASREngine::transcribe_incremental(const AsrRequest& request) {
  AsrPartialHypothesis result{};
  result.sequence          = request.sequence;
  result.created_at        = Clock::now();
  result.stability         = 0.0F;
  result.is_partial        = request.stream_continues;
  result.end_of_utterance  = !request.stream_continues;
  result.revision          = ++revision_;

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

  const std::string raw_text = normalize_whitespace(text);
  const auto current_tokens  = split_tokens(raw_text);
  const auto previous_tokens = split_tokens(last_raw_translation_en_);
  auto committed_tokens      = split_tokens(committed_translation_en_);

  if (!translate_) {
    result.source_text_es = raw_text;
  }

  std::vector<std::string> commit_candidate_tokens;
  std::size_t overlap = 0;

  if (!raw_text.empty() && raw_text == last_raw_translation_en_) {
    ++repeated_hypothesis_count_;
  } else {
    repeated_hypothesis_count_ = raw_text.empty() ? 0U : 1U;
  }

  if (!request.stream_continues) {
    commit_candidate_tokens = current_tokens;
    overlap = current_tokens.size();
  } else if (!previous_tokens.empty() && !current_tokens.empty()) {
    overlap = suffix_prefix_overlap(previous_tokens, current_tokens);
    const bool exact_repeat = (raw_text == last_raw_translation_en_);
    const bool enough_overlap =
        overlap >= 2U || (overlap == 1U && std::max(previous_tokens.size(), current_tokens.size()) <= 2U);
    if (enough_overlap || exact_repeat || repeated_hypothesis_count_ >= 2U) {
      commit_candidate_tokens = previous_tokens;
      if (exact_repeat || repeated_hypothesis_count_ >= 2U) {
        overlap = previous_tokens.size();
      }
    }
  }

  if (!commit_candidate_tokens.empty()) {
    const auto committed_overlap = suffix_prefix_overlap(committed_tokens, commit_candidate_tokens);
    if (committed_overlap < commit_candidate_tokens.size()) {
      committed_tokens.insert(committed_tokens.end(),
                              commit_candidate_tokens.begin() +
                                  static_cast<std::ptrdiff_t>(committed_overlap),
                              commit_candidate_tokens.end());
      result.translated_text_en = join_tokens(
          commit_candidate_tokens, committed_overlap, commit_candidate_tokens.size());
      committed_translation_en_ = join_tokens(committed_tokens, 0, committed_tokens.size());
    }
    result.stable_prefix_en = join_tokens(commit_candidate_tokens, 0, commit_candidate_tokens.size());
  } else {
    result.stable_prefix_en = committed_translation_en_;
  }

  result.raw_translated_text_en = raw_text;
  if (!result.end_of_utterance) {
    result.stability = overlap_stability(overlap, std::max(previous_tokens.size(), current_tokens.size()));
  } else {
    result.stability = raw_text.empty() ? 0.0F : 1.0F;
  }

  last_raw_translation_en_ = raw_text;

  if (result.end_of_utterance) {
    reset_context();
  }

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
  last_raw_translation_en_.clear();
  committed_translation_en_.clear();
  revision_ = 0;
  repeated_hypothesis_count_ = 0;
}

}  // namespace mev
