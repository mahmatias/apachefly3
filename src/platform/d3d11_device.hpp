#pragma once

namespace c3 {

class D3D11Device {
public:
  D3D11Device() = default;
  ~D3D11Device() = default;
  bool init(void* hwnd) { return true; }
  void shutdown() {}
};

} // namespace c3