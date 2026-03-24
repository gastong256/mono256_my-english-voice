#pragma once

#include <cstddef>
#include <deque>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "mev/config/app_config.hpp"

namespace mev {

class DomainContextManager {
 public:
  explicit DomainContextManager(const DomainConfig& config);

  void register_recent_text(const std::string& text);
  void reset_session();

  [[nodiscard]] std::string build_prompt_hint() const;

  [[nodiscard]] std::string normalize(const std::string& input) const;

 private:
  void append_session_term_locked(std::string term);

  std::vector<std::string> base_glossary_;
  std::vector<std::string> frequent_phrases_;
  std::size_t session_terms_limit_;
  bool session_terms_enabled_{true};
  std::string prompt_template_;

  std::unordered_map<std::string, std::string> canonical_terms_;

  mutable std::mutex mutex_;
  std::deque<std::string> recent_terms_;
};

}  // namespace mev
