#pragma once

#include <string>

#include "mev/asr/asr_types.hpp"

namespace mev {

class IASREngine {
 public:
  virtual ~IASREngine() = default;

  virtual bool warmup(std::string& error) = 0;
  [[nodiscard]] virtual AsrPartialHypothesis transcribe_incremental(const AsrRequest& request) = 0;
  [[nodiscard]] virtual std::string name() const = 0;
};

}  // namespace mev
