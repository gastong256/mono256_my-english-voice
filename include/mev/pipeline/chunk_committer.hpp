#pragma once

#include <cstddef>
#include <optional>
#include <string>

#include "mev/asr/asr_types.hpp"
#include "mev/core/types.hpp"

namespace mev {

struct CommittedChunk {
  SequenceNumber sequence{0};
  TimePoint source_started_at{};
  TimePoint committed_at{};
  std::string text{};
};

class IChunkCommitter {
 public:
  virtual ~IChunkCommitter() = default;

  [[nodiscard]] virtual std::optional<CommittedChunk> on_partial(const AsrPartialHypothesis& partial) = 0;
  virtual void reset() = 0;
};

struct ChunkCommitterConfig {
  float stability_threshold{0.72F};
  std::uint32_t force_commit_ms{450};
  std::size_t min_chars{8};
};

class StabilityChunkCommitter final : public IChunkCommitter {
 public:
  explicit StabilityChunkCommitter(ChunkCommitterConfig config);

  [[nodiscard]] std::optional<CommittedChunk> on_partial(const AsrPartialHypothesis& partial) override;
  void reset() override;

 private:
  [[nodiscard]] static std::string trim_copy(std::string text);

  ChunkCommitterConfig config_;
  std::string pending_text_;
  TimePoint pending_since_{};
  std::string last_emitted_accumulated_;
};

}  // namespace mev
