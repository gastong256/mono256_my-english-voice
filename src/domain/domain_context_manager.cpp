#include "mev/domain/domain_context_manager.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>

namespace mev {
namespace {

std::string to_lower(std::string text) {
  std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return text;
}

void replace_all(std::string& target, const std::string& from, const std::string& to) {
  if (from.empty()) {
    return;
  }

  std::size_t pos = 0;
  while ((pos = target.find(from, pos)) != std::string::npos) {
    target.replace(pos, from.size(), to);
    pos += to.size();
  }
}

}  // namespace

DomainContextManager::DomainContextManager(const DomainConfig& config)
    : base_glossary_(config.technical_glossary),
      frequent_phrases_(config.frequent_phrases),
      session_terms_limit_(config.session_terms_limit) {
  for (const auto& term : base_glossary_) {
    canonical_terms_.insert({to_lower(term), term});
  }

  canonical_terms_.insert({"ci cd", "CI/CD"});
  canonical_terms_.insert({"ci slash cd", "CI/CD"});
  canonical_terms_.insert({"postgres", "PostgreSQL"});
  canonical_terms_.insert({"k8s", "Kubernetes"});
}

void DomainContextManager::register_recent_text(const std::string& text) {
  std::lock_guard<std::mutex> lock(mutex_);
  const auto lowered = to_lower(text);

  for (const auto& [token, canonical] : canonical_terms_) {
    if (lowered.find(token) != std::string::npos) {
      append_session_term_locked(canonical);
    }
  }
}

std::string DomainContextManager::build_prompt_hint() const {
  std::lock_guard<std::mutex> lock(mutex_);

  std::ostringstream oss;
  oss << "Technical glossary: ";
  bool first = true;
  for (const auto& term : base_glossary_) {
    if (!first) {
      oss << ", ";
    }
    oss << term;
    first = false;
  }

  if (!recent_terms_.empty()) {
    oss << " | Session terms: ";
    first = true;
    for (const auto& term : recent_terms_) {
      if (!first) {
        oss << ", ";
      }
      oss << term;
      first = false;
    }
  }

  if (!frequent_phrases_.empty()) {
    oss << " | Frequent phrases: ";
    first = true;
    for (const auto& phrase : frequent_phrases_) {
      if (!first) {
        oss << " | ";
      }
      oss << phrase;
      first = false;
    }
  }

  return oss.str();
}

std::string DomainContextManager::normalize(const std::string& input) const {
  auto normalized = input;
  auto lowered = to_lower(input);

  for (const auto& [token, canonical] : canonical_terms_) {
    replace_all(lowered, token, canonical);
  }

  return lowered;
}

void DomainContextManager::append_session_term_locked(std::string term) {
  auto exists = std::find(recent_terms_.begin(), recent_terms_.end(), term);
  if (exists != recent_terms_.end()) {
    recent_terms_.erase(exists);
  }

  recent_terms_.push_back(std::move(term));

  while (recent_terms_.size() > session_terms_limit_) {
    recent_terms_.pop_front();
  }
}

}  // namespace mev
