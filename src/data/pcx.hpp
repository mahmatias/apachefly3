#pragma once

#include <vector>
#include <string>
#include <cstdint>
#include <optional>

namespace c3 {

struct PCXImage {
    uint16_t width = 0;
    uint16_t height = 0;
    uint8_t bits_per_pixel = 0;
    uint8_t num_planes = 0;
    uint16_t bytes_per_line = 0;
    std::vector<uint8_t> palette; // 768 bytes (256 * 3) for 256-color
    std::vector<uint8_t> pixels;  // width * height bytes (8-bit indexed)

    // Convert to RGBA8888
    std::vector<uint32_t> to_rgba() const;
};

std::optional<PCXImage> decode_pcx(const std::vector<uint8_t>& data, size_t offset = 0);
bool write_pcx_png(const PCXImage& img, const std::string& path);

} // namespace c3