#pragma once

namespace mev {

enum class PipelineState { kStopped = 0, kStarting, kRunning, kStopping, kFailed };

class IPipelineOrchestrator {
 public:
  virtual ~IPipelineOrchestrator() = default;

  virtual bool start() = 0;
  virtual void stop() = 0;
  [[nodiscard]] virtual PipelineState state() const = 0;
};

}  // namespace mev
