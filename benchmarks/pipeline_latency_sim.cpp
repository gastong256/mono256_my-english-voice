#include <chrono>
#include <iostream>
#include <memory>
#include <string>

#include "mev/config/app_config.hpp"
#include "mev/core/utterance.hpp"
#include "mev/domain/domain_context_manager.hpp"
#include "mev/domain/technical_domain_adapter.hpp"
#include "mev/pipeline/tts_scheduler.hpp"

int main() {
  auto context = std::make_shared<mev::DomainContextManager>(mev::default_config().domain);
  mev::TechnicalDomainAdapter adapter(context);
  mev::TtsScheduler scheduler(mev::TtsSchedulerPolicy{.max_queue_depth    = 128,
                                                       .backlog_soft_limit = 16,
                                                       .stale_after_ms     = 2000,
                                                       .drop_policy        = mev::DropPolicy::kDropOldest});

  constexpr int rounds = 10000;
  int committed = 0;

  const auto start = std::chrono::steady_clock::now();
  for (int i = 0; i < rounds; ++i) {
    const std::string raw = "we need better observability and tracing for production";

    auto utt = std::make_unique<mev::Utterance>();
    utt->id = static_cast<std::uint64_t>(i);
    utt->state = mev::UtteranceState::NORMALIZING;
    utt->translated_text = raw;
    utt->normalized_text = adapter.correct_asr_output(raw);
    utt->metrics.capture_start = std::chrono::steady_clock::now();

    auto result = scheduler.schedule(std::move(utt), static_cast<std::size_t>(i % 20));
    if (result != nullptr) {
      ++committed;
    }
  }

  const auto elapsed =
      std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start);
  std::cout << "pipeline sim committed=" << committed << " rounds=" << rounds
            << " elapsed_us=" << elapsed.count() << '\n';
  return 0;
}
