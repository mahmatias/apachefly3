#include "data/wav.hpp"

#include <cstring>
#include <iostream>

namespace c3 {

namespace {

uint16_t read_le16(const uint8_t* p) {
    return static_cast<uint16_t>(p[0]) | (static_cast<uint16_t>(p[1]) << 8);
}

uint32_t read_le32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) |
           (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) |
           (static_cast<uint32_t>(p[3]) << 24);
}

bool fourcc_eq(const uint8_t* p, const char* s) {
    return p[0] == s[0] && p[1] == s[1] && p[2] == s[2] && p[3] == s[3];
}

} // namespace

std::optional<WavFile> decode_wav(const std::vector<uint8_t>& data) {
    if (data.size() < 12) {
        std::cerr << "WAV: file too small\n";
        return std::nullopt;
    }

    // RIFF header
    if (!fourcc_eq(data.data(), "RIFF")) {
        std::cerr << "WAV: missing RIFF magic\n";
        return std::nullopt;
    }
    if (!fourcc_eq(data.data() + 8, "WAVE")) {
        std::cerr << "WAV: missing WAVE magic\n";
        return std::nullopt;
    }

    WavFile wav;
    size_t pos = 12;

    while (pos + 8 <= data.size()) {
        const char chunk_id[4] = {
            static_cast<char>(data[pos]),
            static_cast<char>(data[pos+1]),
            static_cast<char>(data[pos+2]),
            static_cast<char>(data[pos+3]),
        };
        uint32_t chunk_size = read_le32(data.data() + pos + 4);
        pos += 8;

        // Clamp chunk size to available data (resources may be slightly
        // truncated relative to the declared RIFF size).
        if (pos + chunk_size > data.size()) {
            chunk_size = data.size() - pos;
        }

        if (std::string(chunk_id, 4) == "fmt ") {
            if (chunk_size < 16) {
                std::cerr << "WAV: fmt chunk too small\n";
                break;
            }
            wav.audio_format = read_le16(data.data() + pos);
            wav.num_channels = read_le16(data.data() + pos + 2);
            wav.sample_rate = read_le32(data.data() + pos + 4);
            wav.byte_rate = read_le32(data.data() + pos + 8);
            wav.block_align = read_le16(data.data() + pos + 12);
            wav.bits_per_sample = read_le16(data.data() + pos + 14);
        } else if (std::string(chunk_id, 4) == "data") {
            wav.data.assign(data.data() + pos,
                            data.data() + pos + chunk_size);
        }

        pos += chunk_size;
        // RIFF chunks are word-aligned (pad byte if odd size)
        if (chunk_size & 1) ++pos;
    }

    return wav;
}

} // namespace c3
