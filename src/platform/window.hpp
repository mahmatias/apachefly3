#pragma once

namespace c3 {

class Window {
public:
  Window() = default;
  ~Window() = default;
  bool create(int width, int height, const char* title) { return true; }
  void poll_events() {}
  bool should_close() const { return false; }
  void swap_buffers() {}
};

} // namespace c3