#include "mev/tts/speech_chunker.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <sstream>

namespace mev {

namespace {

constexpr std::size_t kMaxWordsPerChunk = 6;

std::string trim_copy(const std::string& text) {
  std::size_t begin = 0;
  while (begin < text.size() &&
         std::isspace(static_cast<unsigned char>(text[begin])) != 0) {
    ++begin;
  }

  std::size_t end = text.size();
  while (end > begin &&
         std::isspace(static_cast<unsigned char>(text[end - 1U])) != 0) {
    --end;
  }
  return text.substr(begin, end - begin);
}

std::vector<std::string> split_clauses(const std::string& text) {
  std::vector<std::string> clauses;
  std::string current;

  for (char ch : text) {
    current.push_back(ch);
    if (ch == '.' || ch == ',' || ch == ';' || ch == ':' || ch == '!' || ch == '?') {
      auto clause = trim_copy(current);
      if (!clause.empty()) clauses.push_back(std::move(clause));
      current.clear();
    }
  }

  auto tail = trim_copy(current);
  if (!tail.empty()) clauses.push_back(std::move(tail));
  return clauses;
}

std::vector<std::string> split_clause_by_words(const std::string& clause) {
  std::istringstream input(clause);
  std::vector<std::string> words;
  for (std::string word; input >> word;) {
    words.push_back(std::move(word));
  }

  std::vector<std::string> chunks;
  if (words.empty()) return chunks;

  for (std::size_t offset = 0; offset < words.size(); offset += kMaxWordsPerChunk) {
    const auto end = std::min(words.size(), offset + kMaxWordsPerChunk);
    std::string chunk;
    for (std::size_t i = offset; i < end; ++i) {
      if (!chunk.empty()) chunk.push_back(' ');
      chunk += words[i];
    }
    chunks.push_back(std::move(chunk));
  }
  return chunks;
}

}  // namespace

std::vector<SpeechChunk> chunk_text_for_realtime_tts(const SequenceNumber sequence,
                                                     const std::string& text,
                                                     const bool is_partial,
                                                     const TimePoint now,
                                                     const std::uint32_t chunk_budget_ms) {
  std::vector<SpeechChunk> out;
  auto clauses = split_clauses(trim_copy(text));
  if (clauses.empty()) {
    clauses.push_back(trim_copy(text));
  }

  std::size_t chunk_index = 0;
  for (const auto& clause : clauses) {
    const auto segments = split_clause_by_words(clause);
    for (const auto& segment : segments) {
      if (segment.empty()) continue;
      SpeechChunk chunk;
      chunk.sequence = sequence;
      chunk.text = segment;
      chunk.is_partial = is_partial;
      chunk.deadline_at = now + std::chrono::milliseconds(
          static_cast<int>((chunk_index + 1U) * std::max<std::uint32_t>(chunk_budget_ms, 1U)));
      out.push_back(std::move(chunk));
      ++chunk_index;
    }
  }

  if (!out.empty()) {
    out.back().is_final = !is_partial;
  }

  return out;
}

}  // namespace mev
