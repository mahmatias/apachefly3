#pragma once

namespace c3 {

class Audio {
public:
  Audio() = default;
  ~Audio() = default;
  bool init() { return true; }
  void shutdown() {}
};

} // namespace c3