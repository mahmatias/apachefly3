#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <optional>

namespace c3 {

struct WavFile {
    uint16_t audio_format = 0;
    uint16_t num_channels = 0;
    uint32_t sample_rate = 0;
    uint32_t byte_rate = 0;
    uint16_t block_align = 0;
    uint16_t bits_per_sample = 0;
    std::vector<uint8_t> data;
};

std::optional<WavFile> decode_wav(const std::vector<uint8_t>& data);

} // namespace c3
