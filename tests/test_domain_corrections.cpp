// test_domain_corrections.cpp
// Tests TechnicalDomainAdapter:
//   - correct_asr_output() fixes mis-transcriptions
//   - prepare_for_tts() applies pronunciation hints
//   - generate_asr_prompt() includes glossary terms

#include <cassert>
#include <memory>
#include <string>

#include "mev/config/app_config.hpp"
#include "mev/domain/domain_context_manager.hpp"
#include "mev/domain/technical_domain_adapter.hpp"

using namespace mev;

static TechnicalDomainAdapter make_adapter() {
  DomainConfig cfg;
  auto ctx = std::make_shared<DomainContextManager>(cfg);
  TechnicalDomainAdapter adapter(std::move(ctx));
  std::string error;
  const bool ok = adapter.initialize(cfg, error);
  assert(ok && "domain adapter initialization must succeed");
  (void)error;
  return adapter;
}

static void test_asr_correction_postgres() {
  auto adapter = make_adapter();
  const auto result = adapter.correct_asr_output("We used post grass for the database");
  assert(result.find("PostgreSQL") != std::string::npos &&
         "'post grass' must be corrected to 'PostgreSQL'");
}

static void test_asr_correction_kubectl() {
  auto adapter = make_adapter();
  const auto result = adapter.correct_asr_output("We ran cube cuddle get pods");
  assert(result.find("kubectl") != std::string::npos &&
         "'cube cuddle' must be corrected to 'kubectl'");
}

static void test_asr_correction_ci_cd() {
  auto adapter = make_adapter();
  const auto result = adapter.correct_asr_output("Our ci cd pipeline runs on GitHub Actions");
  assert(result.find("CI/CD") != std::string::npos &&
         "'ci cd' must be corrected to 'CI/CD'");
}

static void test_asr_correction_no_change_for_correct_text() {
  auto adapter = make_adapter();
  const std::string input = "We use Kubernetes for container orchestration";
  const auto result = adapter.correct_asr_output(input);
  // Should not mangle correct text.
  assert(!result.empty());
}

static void test_asr_correction_prefers_simple_english() {
  auto adapter = make_adapter();
  const auto result = adapter.correct_asr_output(
      "From a scalability perspective we are going to reduce the high latency issue");
  assert(result.find("for scalability") != std::string::npos &&
         "verbose transition phrase must be simplified");
  assert(result.find("we will") != std::string::npos &&
         "future intent should prefer short English");
  assert(result.find("high latency") != std::string::npos &&
         "the phrase should keep the concise latency wording");
}

static void test_tts_pronunciation_kubectl() {
  auto adapter = make_adapter();
  const auto result = adapter.prepare_for_tts("Run kubectl get pods");
  assert(result.find("kube control") != std::string::npos &&
         "'kubectl' must be expanded to 'kube control' for TTS");
}

static void test_tts_pronunciation_nginx() {
  auto adapter = make_adapter();
  const auto result = adapter.prepare_for_tts("nginx is a reverse proxy");
  assert(result.find("engine X") != std::string::npos &&
         "'nginx' must be expanded for TTS");
}

static void test_tts_pronunciation_k8s() {
  auto adapter = make_adapter();
  const auto result = adapter.prepare_for_tts("We deploy to k8s using CI/CD");
  // normalize() maps k8s → "Kubernetes" (canonical form) before tts_pronunciation_ runs.
  assert(result.find("Kubernetes") != std::string::npos &&
         "'k8s' must expand to 'Kubernetes'");
  assert(result.find("C I C D") != std::string::npos &&
         "'CI/CD' must expand to 'C I C D'");
}

static void test_generate_asr_prompt_non_empty() {
  auto adapter = make_adapter();
  const auto prompt = adapter.generate_asr_prompt();
  assert(!prompt.empty() && "ASR prompt must not be empty");
  // Should contain something recognizable from the default glossary.
  assert(prompt.find("Technical") != std::string::npos ||
         prompt.find("Kubernetes") != std::string::npos ||
         prompt.find("PostgreSQL") != std::string::npos);
  assert(prompt.size() <= 220 && "ASR prompt must stay compact for low-latency prompting");
}

static void test_session_context_updates_prompt() {
  auto adapter = make_adapter();
  const auto prompt_before = adapter.generate_asr_prompt();

  adapter.update_session_context("We discussed RabbitMQ and Redis today");
  const auto prompt_after = adapter.generate_asr_prompt();

  // After seeing technical terms, the prompt should include session terms.
  assert(!prompt_after.empty());
  assert(prompt_after != prompt_before && "prompt should reflect newly learned session terms");
  assert(prompt_after.find("RabbitMQ") != std::string::npos &&
         "prompt should include the new session term");
}

static void test_reset_session() {
  auto adapter = make_adapter();
  adapter.update_session_context("RabbitMQ Redis Terraform");
  const auto prompt_before_reset = adapter.generate_asr_prompt();
  adapter.reset_session();  // must not throw
  const auto prompt_after_reset = adapter.generate_asr_prompt();
  assert(!prompt_after_reset.empty());
  assert(prompt_before_reset.find("RabbitMQ") != std::string::npos &&
         "sanity check: session term should have been registered before reset");
  assert(prompt_after_reset.find("RabbitMQ") == std::string::npos &&
         "reset must clear session-specific prompt terms");
}

int main() {
  test_asr_correction_postgres();
  test_asr_correction_kubectl();
  test_asr_correction_ci_cd();
  test_asr_correction_no_change_for_correct_text();
  test_asr_correction_prefers_simple_english();
  test_tts_pronunciation_kubectl();
  test_tts_pronunciation_nginx();
  test_tts_pronunciation_k8s();
  test_generate_asr_prompt_non_empty();
  test_session_context_updates_prompt();
  test_reset_session();
  return 0;
}
