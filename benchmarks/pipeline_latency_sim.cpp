#include <algorithm>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <memory>
#include <numeric>
#include <string>
#include <string_view>
#include <vector>

#include "mev/config/app_config.hpp"
#include "mev/core/utterance.hpp"
#include "mev/domain/domain_context_manager.hpp"
#include "mev/domain/technical_domain_adapter.hpp"
#include "mev/pipeline/tts_scheduler.hpp"
#include "mev/tts/speech_chunker.hpp"

namespace {

struct CliOptions {
  std::string mode{"interactive_balanced"};
  std::string summary_out{};
  int rounds{2000};
};

struct Summary {
  std::string mode;
  int rounds{0};
  int scheduled{0};
  int dropped{0};
  int partial_kept{0};
  int final_kept{0};
  double avg_selected_chunks{0.0};
  std::uint64_t p50_scheduler_us{0};
  std::uint64_t p95_scheduler_us{0};
};

std::uint64_t percentile_us(std::vector<std::uint64_t> values, const double quantile) {
  if (values.empty()) return 0;
  std::sort(values.begin(), values.end());
  const auto index = static_cast<std::size_t>(
      std::clamp(quantile, 0.0, 1.0) * static_cast<double>(values.size() - 1U));
  return values[index];
}

CliOptions parse_args(const int argc, char** argv) {
  CliOptions options;
  for (int i = 1; i < argc; ++i) {
    const std::string_view arg{argv[i]};
    if (arg == "--mode" && (i + 1) < argc) {
      options.mode = argv[++i];
      continue;
    }
    if (arg == "--summary-out" && (i + 1) < argc) {
      options.summary_out = argv[++i];
      continue;
    }
    if (arg == "--rounds" && (i + 1) < argc) {
      options.rounds = std::max(1, std::stoi(argv[++i]));
      continue;
    }
    std::cerr << "Unknown argument: " << arg << "\n";
    std::cerr << "Usage: benchmark_pipeline_latency [--mode interactive_preview|interactive_balanced] "
                 "[--summary-out path.json] [--rounds 2000]\n";
    std::exit(2);
  }
  return options;
}

mev::AppConfig make_config_for_mode(const std::string& mode) {
  auto cfg = mev::default_config();
  cfg.tts.mode = mode;
  if (mode == "interactive_preview") {
    cfg.audio.frames_per_buffer = 160;
    cfg.pipeline.max_queue_depth = 4;
    cfg.pipeline.warning_threshold_ms = 1200;
    cfg.pipeline.critical_threshold_ms = 2200;
    cfg.pipeline.stale_after_ms = 1200;
    cfg.pipeline.stale_after_n_newer = 1;
    cfg.tts.max_primary_tts_budget_ms = 120;
  } else {
    cfg.audio.frames_per_buffer = 160;
    cfg.pipeline.max_queue_depth = 4;
    cfg.pipeline.warning_threshold_ms = 1200;
    cfg.pipeline.critical_threshold_ms = 2200;
    cfg.pipeline.stale_after_ms = 1200;
    cfg.pipeline.stale_after_n_newer = 1;
    cfg.tts.max_primary_tts_budget_ms = 180;
  }
  return cfg;
}

std::string render_json(const Summary& summary) {
  std::ostringstream out;
  out << "{\n"
      << "  \"mode\": \"" << summary.mode << "\",\n"
      << "  \"rounds\": " << summary.rounds << ",\n"
      << "  \"scheduled\": " << summary.scheduled << ",\n"
      << "  \"dropped\": " << summary.dropped << ",\n"
      << "  \"partial_kept\": " << summary.partial_kept << ",\n"
      << "  \"final_kept\": " << summary.final_kept << ",\n"
      << "  \"avg_selected_chunks\": " << summary.avg_selected_chunks << ",\n"
      << "  \"p50_scheduler_us\": " << summary.p50_scheduler_us << ",\n"
      << "  \"p95_scheduler_us\": " << summary.p95_scheduler_us << "\n"
      << "}\n";
  return out.str();
}

}  // namespace

int main(int argc, char** argv) {
  const auto options = parse_args(argc, argv);
  auto config = make_config_for_mode(options.mode);

  auto context = std::make_shared<mev::DomainContextManager>(config.domain);
  mev::TechnicalDomainAdapter adapter(context);
  std::string init_error;
  if (!adapter.initialize(config.domain, init_error)) {
    std::cerr << "[FAIL] benchmark_pipeline_latency: domain adapter init failed: "
              << init_error << "\n";
    return 1;
  }

  mev::TtsScheduler scheduler(mev::TtsSchedulerPolicy{
      .max_queue_depth = config.pipeline.max_queue_depth,
      .backlog_soft_limit = static_cast<std::size_t>(config.pipeline.max_queue_depth / 2U),
      .partial_backlog_limit = 1U,
      .output_backlog_limit = config.pipeline.max_queue_depth + 1U,
      .stale_after_ms = config.pipeline.stale_after_ms,
      .stale_after_n_newer = config.pipeline.stale_after_n_newer,
      .chunk_deadline_slack_ms = 30U,
      .drop_policy = config.pipeline.drop_policy,
  });

  const std::vector<std::string> phrases = {
      "we need to review the backend architecture before release",
      "latency is high in the database for PostgreSQL",
      "we will deploy Redis today with little margin",
      "the bottleneck is now in Kubernetes",
      "we need rate limiting for FastAPI this week",
  };

  std::vector<std::uint64_t> timings_us;
  timings_us.reserve(static_cast<std::size_t>(options.rounds));

  Summary summary;
  summary.mode = options.mode;
  summary.rounds = options.rounds;
  double total_selected_chunks = 0.0;

  for (int i = 0; i < options.rounds; ++i) {
    const bool is_partial = (i % 3) != 2;
    auto utt = std::make_unique<mev::Utterance>();
    utt->id = static_cast<std::uint64_t>(i + 1);
    utt->state = mev::UtteranceState::NORMALIZING;
    utt->asr_is_partial = is_partial;
    utt->translated_text = phrases[static_cast<std::size_t>(i) % phrases.size()];
    utt->normalized_text = adapter.prepare_for_tts(
        adapter.correct_asr_output(utt->translated_text));
    utt->metrics.capture_start = std::chrono::steady_clock::now();

    const auto t0 = std::chrono::steady_clock::now();
    auto scheduled = scheduler.schedule(std::move(utt),
                                        static_cast<std::size_t>(i) %
                                            (config.pipeline.max_queue_depth + 2U));
    if (!scheduled) {
      ++summary.dropped;
      continue;
    }

    scheduled->speech_chunks = mev::chunk_text_for_realtime_tts(
        static_cast<mev::SequenceNumber>(scheduled->id), scheduled->normalized_text,
        scheduled->asr_is_partial, std::chrono::steady_clock::now(),
        config.tts.max_primary_tts_budget_ms);
    const auto selected = scheduler.select_chunks_for_synthesis(
        *scheduled, std::chrono::steady_clock::now(),
        static_cast<std::size_t>(i) % (config.pipeline.max_queue_depth + 3U));
    const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - t0);
    timings_us.push_back(static_cast<std::uint64_t>(elapsed.count()));

    ++summary.scheduled;
    total_selected_chunks += static_cast<double>(selected.size());
    if (scheduled->asr_is_partial) {
      ++summary.partial_kept;
    } else {
      ++summary.final_kept;
    }
  }

  if (summary.scheduled > 0) {
    summary.avg_selected_chunks = total_selected_chunks /
                                  static_cast<double>(summary.scheduled);
  }
  summary.p50_scheduler_us = percentile_us(timings_us, 0.50);
  summary.p95_scheduler_us = percentile_us(timings_us, 0.95);

  const auto rendered = render_json(summary);
  std::cout << rendered;

  if (!options.summary_out.empty()) {
    std::ofstream output(options.summary_out, std::ios::binary);
    if (!output) {
      std::cerr << "[FAIL] benchmark_pipeline_latency: cannot write summary to "
                << options.summary_out << "\n";
      return 1;
    }
    output << rendered;
  }

  return 0;
}
