#include "mev/domain/technical_domain_adapter.hpp"

#include <algorithm>
#include <cctype>

#include "mev/core/logger.hpp"

#define TOML_EXCEPTIONS 1
#include <toml++/toml.hpp>

namespace mev {

namespace {
std::string to_lower_str(const std::string& s) {
  std::string out;
  out.reserve(s.size());
  for (unsigned char c : s) {
    out.push_back(static_cast<char>(std::tolower(c)));
  }
  return out;
}

void replace_all_ci(std::string& target, const std::string& from, const std::string& to) {
  // Case-insensitive search, replace with `to`.
  if (from.empty()) return;
  std::string lower_target = to_lower_str(target);
  const std::string lower_from = to_lower_str(from);
  std::size_t pos = 0;
  while ((pos = lower_target.find(lower_from, pos)) != std::string::npos) {
    target.replace(pos, from.size(), to);
    lower_target.replace(pos, from.size(), to_lower_str(to));
    pos += to.size();
  }
}
}  // namespace

TechnicalDomainAdapter::TechnicalDomainAdapter(
    std::shared_ptr<DomainContextManager> context_manager)
    : context_manager_(std::move(context_manager)) {
  load_default_corrections();
  load_default_pronunciation();
}

bool TechnicalDomainAdapter::initialize(const DomainConfig& config, std::string& /*error*/) {
  // Try to load corrections and pronunciation from the TOML glossary file.
  if (!config.glossary_path.empty()) {
    try {
      const auto tbl = toml::parse_file(config.glossary_path);

      // [corrections] — override defaults with file entries.
      if (const auto* corrections = tbl["corrections"].as_table()) {
        corrections->for_each([&](const toml::key& k, const auto& v) {
          if (const auto* sv = v.as_string()) {
            asr_corrections_[std::string(k.str())] = sv->get();
          }
        });
        MEV_LOG_INFO("TechnicalDomainAdapter: loaded ", corrections->size(),
                     " corrections from '", config.glossary_path, "'");
      }

      // [pronunciation] — override defaults with file entries.
      if (const auto* pronunciation = tbl["pronunciation"].as_table()) {
        pronunciation->for_each([&](const toml::key& k, const auto& v) {
          if (const auto* sv = v.as_string()) {
            tts_pronunciation_[std::string(k.str())] = sv->get();
          }
        });
        MEV_LOG_INFO("TechnicalDomainAdapter: loaded ", pronunciation->size(),
                     " pronunciation hints from '", config.glossary_path, "'");
      }
    } catch (const toml::parse_error& e) {
      MEV_LOG_WARN("TechnicalDomainAdapter: failed to parse '", config.glossary_path,
                   "': ", e.description());
      // Non-fatal: use hardcoded defaults.
    } catch (const std::exception& e) {
      MEV_LOG_WARN("TechnicalDomainAdapter: could not open '", config.glossary_path,
                   "': ", e.what());
    }
  }

  MEV_LOG_INFO("TechnicalDomainAdapter initialised");
  return true;
}

std::string TechnicalDomainAdapter::generate_asr_prompt() const {
  return context_manager_->build_prompt_hint();
}

std::string TechnicalDomainAdapter::correct_asr_output(const std::string& raw_text) {
  auto corrected = raw_text;
  for (const auto& [mis, canonical] : asr_corrections_) {
    replace_all_ci(corrected, mis, canonical);
  }
  return corrected;
}

std::string TechnicalDomainAdapter::prepare_for_tts(const std::string& text) {
  // First run through the DomainContextManager for canonical casing,
  // then apply pronunciation expansions.
  auto prepared = context_manager_->normalize(text);
  for (const auto& [term, pronunciation] : tts_pronunciation_) {
    replace_all_ci(prepared, term, pronunciation);
  }
  return prepared;
}

void TechnicalDomainAdapter::update_session_context(const std::string& recognized_text) {
  context_manager_->register_recent_text(recognized_text);
}

void TechnicalDomainAdapter::reset_session() {
  MEV_LOG_INFO("TechnicalDomainAdapter::reset_session (no-op in current DomainContextManager)");
}

void TechnicalDomainAdapter::load_default_corrections() {
  // Common Whisper mis-transcriptions for Spanish-accented English technical speech.
  asr_corrections_ = {
      {"post-grass", "PostgreSQL"},
      {"post grass", "PostgreSQL"},
      {"post gray sql", "PostgreSQL"},
      {"cube ctl", "kubectl"},
      {"cube cuddle", "kubectl"},
      {"cube control", "kubectl"},
      {"kube ctl", "kubectl"},
      {"doctor", "Docker"},
      {"terra farm", "Terraform"},
      {"terra form", "Terraform"},
      {"fast api", "FastAPI"},
      {"read is", "Redis"},
      {"red is", "Redis"},
      {"cube ernet ease", "Kubernetes"},
      {"cube net ease", "Kubernetes"},
      {"kubernetes ease", "Kubernetes"},
      {"engine x", "nginx"},
      {"engine ex", "nginx"},
      {"jay son", "JSON"},
      {"jay son schema", "JSON Schema"},
      {"ci cd", "CI/CD"},
      {"ci slash cd", "CI/CD"},
      {"s l o", "SLO"},
      {"s l a", "SLA"},
  };
}

void TechnicalDomainAdapter::load_default_pronunciation() {
  // Pronunciation hints: these are applied pre-TTS so the synthesizer
  // reads technical terms correctly.
  tts_pronunciation_ = {
      {"kubectl", "kube control"},
      {"nginx", "engine X"},
      {"PostgreSQL", "post gres Q L"},
      {"CI/CD", "C I C D"},
      {"async", "ay-sync"},
      {"k8s", "kubernetes"},
      {"SLO", "S L O"},
      {"SLA", "S L A"},
      {"gRPC", "G R P C"},
      {"OAuth", "O Auth"},
      {"DevOps", "dev ops"},
      {"IaC", "infrastructure as code"},
      {"YAML", "yamel"},
      {"FastAPI", "fast A P I"},
  };
}

}  // namespace mev
