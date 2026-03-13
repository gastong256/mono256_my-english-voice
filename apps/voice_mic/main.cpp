#include <string>

#include "mev/app/application.hpp"

int main(int argc, char** argv) {
  const std::string config_path = argc > 1 ? argv[1] : "configs/default.ini";
  mev::Application app(config_path);
  return app.run();
}
