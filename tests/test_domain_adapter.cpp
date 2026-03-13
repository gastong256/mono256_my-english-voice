#include <cassert>
#include <memory>
#include <string>

#include "mev/config/app_config.hpp"
#include "mev/domain/domain_context_manager.hpp"
#include "mev/domain/technical_domain_adapter.hpp"

// Updated for new IDomainAdapter interface (generate_asr_prompt, correct_asr_output,
// prepare_for_tts, update_session_context).  The old normalize_for_tts / prompt_hint
// have been split into the correct stage-specific methods.

int main() {
  auto context = std::make_shared<mev::DomainContextManager>(mev::default_config().domain);
  mev::TechnicalDomainAdapter adapter(context);

  // ASR prompt must be non-empty and contain recognisable glossary content.
  const auto prompt = adapter.generate_asr_prompt();
  assert(!prompt.empty());
  assert(prompt.find("Technical") != std::string::npos ||
         prompt.find("Kubernetes") != std::string::npos);

  // ASR corrections: common mis-transcriptions must be fixed.
  const auto corrected = adapter.correct_asr_output("We used post grass and cube cuddle");
  assert(corrected.find("PostgreSQL") != std::string::npos);
  assert(corrected.find("kubectl") != std::string::npos);

  // prepare_for_tts must expand pronunciation hints.
  const auto tts_text = adapter.prepare_for_tts("kubectl get pods on k8s");
  assert(tts_text.find("kube control") != std::string::npos);

  // Session updates and reset must not crash.
  adapter.update_session_context("Kubernetes Redis CI/CD pipeline");
  assert(!adapter.generate_asr_prompt().empty());
  adapter.reset_session();

  return 0;
}
