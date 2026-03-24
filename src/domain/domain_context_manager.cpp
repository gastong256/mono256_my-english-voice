#include "mev/domain/domain_context_manager.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <sstream>

#include "mev/core/logger.hpp"

#define TOML_EXCEPTIONS 1
#include <toml++/toml.hpp>

namespace mev {
namespace {

constexpr std::size_t kPromptBaseTermsLimit = 8;
constexpr std::size_t kPromptSessionTermsLimit = 4;
constexpr std::size_t kPromptFrequentPhrasesLimit = 3;
constexpr std::size_t kPromptCharBudget = 220;

std::string to_lower(std::string text) {
  std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return text;
}

void replace_all_ci(std::string& target, const std::string& from, const std::string& to) {
  if (from.empty()) {
    return;
  }

  std::string lower_target = to_lower(target);
  const std::string lower_from = to_lower(from);
  std::size_t pos = 0;
  while ((pos = lower_target.find(lower_from, pos)) != std::string::npos) {
    target.replace(pos, from.size(), to);
    lower_target.replace(pos, from.size(), to_lower(to));
    pos += to.size();
  }
}

std::vector<std::string> load_string_array(const toml::table& root, const char* table_name,
                                           const char* key_name) {
  std::vector<std::string> values;
  const auto* table = root[table_name].as_table();
  if (table == nullptr) return values;
  const auto* array = (*table)[key_name].as_array();
  if (array == nullptr) return values;

  array->for_each([&](const auto& item) {
    if (const auto* text = item.as_string()) {
      values.push_back(text->get());
    }
  });
  return values;
}

void append_items_with_budget(std::ostringstream& oss, const char* label,
                              const std::vector<std::string>& items,
                              const std::size_t max_items, std::size_t& used_chars) {
  if (items.empty() || max_items == 0U || used_chars >= kPromptCharBudget) {
    return;
  }

  std::ostringstream local;
  local << label;
  bool added = false;
  for (std::size_t i = 0; i < std::min(items.size(), max_items); ++i) {
    if (added) local << ", ";
    local << items[i];
    added = true;
  }

  const std::string chunk = local.str();
  if (!added || used_chars + chunk.size() > kPromptCharBudget) {
    return;
  }

  if (used_chars != 0U) {
    oss << " | ";
    used_chars += 3U;
  }
  oss << chunk;
  used_chars += chunk.size();
}

}  // namespace

DomainContextManager::DomainContextManager(const DomainConfig& config)
    : base_glossary_(config.technical_glossary),
      frequent_phrases_(config.frequent_phrases),
      session_terms_limit_(config.session_terms_limit),
      session_terms_enabled_(config.session_terms_enabled),
      prompt_template_(config.initial_prompt_template) {
  if (!config.glossary_path.empty() && std::filesystem::exists(config.glossary_path)) {
    try {
      const auto root = toml::parse_file(config.glossary_path);
      auto loaded_terms = load_string_array(root, "domain_terms", "terms");
      auto loaded_phrases = load_string_array(root, "frequent_phrases", "phrases");
      if (!loaded_terms.empty()) {
        base_glossary_ = std::move(loaded_terms);
      }
      if (!loaded_phrases.empty()) {
        frequent_phrases_ = std::move(loaded_phrases);
      }
    } catch (const std::exception& e) {
      MEV_LOG_WARN("DomainContextManager: failed to load glossary prompt terms from '",
                   config.glossary_path, "': ", e.what());
    }
  }

  for (const auto& term : base_glossary_) {
    canonical_terms_.insert({to_lower(term), term});
  }

  canonical_terms_.insert({"ci cd", "CI/CD"});
  canonical_terms_.insert({"ci slash cd", "CI/CD"});
  canonical_terms_.insert({"postgres", "PostgreSQL"});
  canonical_terms_.insert({"k8s", "Kubernetes"});
  canonical_terms_.insert({"message queue", "message queue"});
  canonical_terms_.insert({"dead letter queue", "dead letter queue"});
  canonical_terms_.insert({"rate limiting", "rate limiting"});
  canonical_terms_.insert({"load balancer", "load balancer"});
}

void DomainContextManager::register_recent_text(const std::string& text) {
  if (!session_terms_enabled_) {
    return;
  }
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
  std::size_t used_chars = 0;

  std::string intro = prompt_template_;
  replace_all_ci(intro, "{topics}",
                 base_glossary_.empty() ? std::string("technical discussion")
                                        : base_glossary_.front());
  if (intro.size() > 72U) {
    intro = "Technical meeting. Prefer simple English and canonical technical terms.";
  }
  oss << intro;
  used_chars += intro.size();

  append_items_with_budget(oss, "Terms: ", base_glossary_, kPromptBaseTermsLimit, used_chars);

  std::vector<std::string> recent_terms(recent_terms_.begin(), recent_terms_.end());
  if (session_terms_enabled_) {
    std::reverse(recent_terms.begin(), recent_terms.end());
    append_items_with_budget(oss, "Session: ", recent_terms, kPromptSessionTermsLimit, used_chars);
  }
  append_items_with_budget(oss, "Phrases: ", frequent_phrases_, kPromptFrequentPhrasesLimit,
                           used_chars);

  return oss.str();
}

std::string DomainContextManager::normalize(const std::string& input) const {
  auto normalized = input;
  for (const auto& [token, canonical] : canonical_terms_) {
    replace_all_ci(normalized, token, canonical);
  }

  return normalized;
}

void DomainContextManager::reset_session() {
  std::lock_guard<std::mutex> lock(mutex_);
  recent_terms_.clear();
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
