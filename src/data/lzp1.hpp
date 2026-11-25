#pragma once

#include <vector>
#include <cstdint>
#include <optional>

namespace c3::data {

// LZP1 decompression (GIF-style LZW, 9-13 bits, 8192-entry dictionary)
// Based on analysis of Comanche 3 LZP1 format
std::optional<std::vector<uint8_t>> lzp1_decompress(const uint8_t* src, size_t src_size);

} // namespace c3::data