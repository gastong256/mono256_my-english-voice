#pragma once

#include <memory>
#include <string>

#include "mev/config/app_config.hpp"

namespace mev {

class Application {
 public:
  explicit Application(std::string config_path);

  [[nodiscard]] int run() const;

 private:
  std::string config_path_;
};

}  // namespace mev
