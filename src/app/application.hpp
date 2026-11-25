#pragma once

#include <string>
#include <filesystem>

namespace c3 {

class Application {
public:
  explicit Application(const std::filesystem::path& iso_path);
  ~Application();

  int run();

private:
  std::filesystem::path iso_path_;
};

} // namespace c3