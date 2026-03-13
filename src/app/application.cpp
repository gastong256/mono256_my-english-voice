#include "mev/app/application.hpp"

#include <chrono>
#include <thread>

#include "mev/core/logger.hpp"
#include "mev/pipeline/pipeline_orchestrator.hpp"

namespace mev {

Application::Application(std::string config_path) : config_path_(std::move(config_path)) {}

int Application::run() const {
  AppConfig config = default_config();
  std::string error;
  if (!load_config_from_file(config_path_, config, error)) {
    MEV_LOG_ERROR("failed to load config: ", error);
    return 1;
  }

  PipelineOrchestrator orchestrator(config);
  if (!orchestrator.start()) {
    MEV_LOG_ERROR("failed to start pipeline");
    return 2;
  }

  std::this_thread::sleep_for(std::chrono::seconds(config.runtime.run_duration_seconds));
  orchestrator.stop();

  const auto snapshot = orchestrator.metrics_snapshot();
  MEV_LOG_INFO("shutdown metrics queue_drops=", snapshot.queue_drops,
               " output_underruns=", snapshot.output_underruns, " stale_cancelled=",
               snapshot.stale_cancelled);

  return 0;
}

}  // namespace mev
