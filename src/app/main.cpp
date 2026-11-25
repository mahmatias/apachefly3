#include "application.hpp"
#include <iostream>
#include <filesystem>

int main(int argc, char* argv[]) {
  if (argc < 3) {
    std::cerr << "Usage: comanche3 --iso <path_to_iso> [--run]" << std::endl;
    return 1;
  }

  std::filesystem::path iso_path;
  bool run_mode = false;

  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "--iso" && i + 1 < argc) {
      iso_path = argv[++i];
    } else if (arg == "--run") {
      run_mode = true;
    }
  }

  if (iso_path.empty()) {
    std::cerr << "Error: --iso parameter required" << std::endl;
    return 1;
  }

  // Allow directories (mount points) and regular files
  std::error_code ec;
  if (!std::filesystem::exists(iso_path, ec)) {
    std::cerr << "Error: ISO not found: " << iso_path << std::endl;
    return 1;
  }

  c3::Application app(iso_path);
  return app.run();
}