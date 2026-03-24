#include "mev/asr/whisper_asr_stub.hpp"

#include <array>

namespace mev {

namespace {

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

std::size_t prefix_overlap(const std::vector<std::string>& left,
                           const std::vector<std::string>& right) {
  const auto max_overlap = std::min(left.size(), right.size());
  std::size_t overlap = 0;
  while (overlap < max_overlap && left[overlap] == right[overlap]) {
    ++overlap;
  }
  return overlap;
}

}  // namespace

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
  static constexpr std::array<const char*, 5> kEsText = {
      "necesitamos",
      "necesitamos revisar",
      "necesitamos revisar la arquitectura",
      "necesitamos revisar la arquitectura de backend",
      "necesitamos revisar la arquitectura de backend antes del release",
  };

  static constexpr std::array<const char*, 5> kEnText = {
      "we need",
      "we need to review",
      "we need to review the architecture",
      "we need to review the backend architecture",
      "we need to review the backend architecture before release",
  };

  AsrPartialHypothesis out;
  out.sequence         = request.sequence;
  out.created_at       = Clock::now();
  out.is_partial       = request.stream_continues;
  out.end_of_utterance = !request.stream_continues;
  out.revision         = ++revision_;

  const auto idx = request.stream_continues
                       ? std::min(partial_step_, kEnText.size() - 2U)
                       : (kEnText.size() - 1U);
  const std::string current_es = kEsText[idx];
  const std::string current_en = kEnText[idx];
  out.source_text_es = current_es;
  out.raw_translated_text_en = current_en;

  const auto previous_tokens = split_tokens(last_raw_translation_en_);
  const auto current_tokens  = split_tokens(current_en);
  auto committed_tokens      = split_tokens(committed_translation_en_);

  std::vector<std::string> commit_candidate_tokens;
  if (!request.stream_continues) {
    commit_candidate_tokens = current_tokens;
    out.stability = current_tokens.empty() ? 0.0F : 1.0F;
  } else if (!previous_tokens.empty()) {
    const auto overlap = prefix_overlap(previous_tokens, current_tokens);
    if (overlap == previous_tokens.size()) {
      commit_candidate_tokens = previous_tokens;
    }
    out.stability = current_tokens.empty()
                        ? 0.0F
                        : static_cast<float>(overlap) / static_cast<float>(current_tokens.size());
  }

  if (!commit_candidate_tokens.empty()) {
    const auto committed_overlap = prefix_overlap(committed_tokens, commit_candidate_tokens);
    if (committed_overlap < commit_candidate_tokens.size()) {
      out.translated_text_en = join_tokens(commit_candidate_tokens, committed_overlap,
                                           commit_candidate_tokens.size());
      committed_tokens.insert(committed_tokens.end(),
                              commit_candidate_tokens.begin() +
                                  static_cast<std::ptrdiff_t>(committed_overlap),
                              commit_candidate_tokens.end());
      committed_translation_en_ = join_tokens(committed_tokens, 0, committed_tokens.size());
    }
    out.stable_prefix_en = join_tokens(commit_candidate_tokens, 0, commit_candidate_tokens.size());
  } else {
    out.stable_prefix_en = committed_translation_en_;
  }

  last_raw_translation_en_ = current_en;
  if (request.stream_continues) {
    ++partial_step_;
  } else {
    partial_step_ = 0;
    last_raw_translation_en_.clear();
    committed_translation_en_.clear();
  }

  counter_.fetch_add(1, std::memory_order_relaxed);
  return out;
}

}  // namespace mev
