#include "data/lzp1.hpp"
#include <vector>
#include <cstdint>
#include <optional>
#include <iostream>

namespace c3::data {

struct DictEntry {
    uint16_t prefix;
    uint8_t  ch;
};

static inline uint16_t read_9bits_lsb(const uint8_t* data, size_t compressed_size, size_t pos) {
    uint16_t code = 0;
    for (int b = 0; b < 9; b++) {
        size_t byte = (pos + b) / 8;
        int bit = (pos + b) % 8;
        if (byte >= compressed_size) return 0;
        code |= ((data[byte] >> bit) & 1) << b;
    }
    return code;
}

static inline uint16_t read_9bits_msb(const uint8_t* data, size_t compressed_size, size_t pos) {
    uint16_t code = 0;
    for (int b = 0; b < 9; b++) {
        size_t byte = (pos + b) / 8;
        int bit = 7 - ((pos + b) % 8);
        if (byte >= compressed_size) return 0;
        code |= ((data[byte] >> bit) & 1) << b;
    }
    return code;
}

static std::optional<std::vector<uint8_t>> try_decompress_from(
    const uint8_t* compressed_data, size_t compressed_size,
    size_t start_bit, uint32_t expected_size, bool msb_first) {

    const uint16_t CLEAR_CODE = 256;
    const uint16_t END_CODE = 257;
    const uint16_t FIRST_FREE = 258;

    DictEntry dict[8192];
    uint16_t dict_size = 256;
    for (uint16_t i = 0; i < 256; ++i) {
        dict[i].prefix = 0xFFFF;
        dict[i].ch = static_cast<uint8_t>(i);
    }

    std::vector<uint8_t> output;
    output.reserve(expected_size);

    size_t bit_pos = start_bit;
    int bit_width = 9;
    uint16_t max_code = 511;
    uint16_t prev_code = 0xFFFF;
    size_t max_bits = compressed_size * 8;

    auto read_bits = [&](int n) -> uint16_t {
        uint32_t val = 0;
        for (int b = 0; b < n; b++) {
            if (bit_pos >= max_bits) return 0xFFFF;  // Signal end
            size_t byte = bit_pos / 8;
            int bit = msb_first ? (7 - (bit_pos % 8)) : (bit_pos % 8);
            if (byte >= compressed_size) return 0xFFFF;
            val |= ((compressed_data[byte] >> bit) & 1) << b;
            bit_pos++;
        }
        return static_cast<uint16_t>(val);
    };

    // Verify first code is CLEAR
    uint16_t first = read_bits(9);
    std::cerr << "  [debug] try_decompress_from start_bit=" << start_bit << " msb=" << msb_first << " first_code=" << first << " CLEAR_CODE=" << CLEAR_CODE << "\n";
    if (first != CLEAR_CODE) {
        std::cerr << "  First code not CLEAR: " << first << "\n";
        return std::nullopt;
    }

    // Safety limits - stop at expected size to avoid garbage after valid data
    size_t max_output = expected_size;
    size_t max_iterations = max_bits;  // Each iteration consumes at least 1 bit

    size_t iterations = 0;
    std::cerr << "  [debug] Starting loop: max_bits=" << max_bits << " max_iter=" << max_iterations << " max_out=" << max_output << "\n";
    while (bit_pos < max_bits && iterations < max_iterations && output.size() < max_output) {
        iterations++;
        uint16_t code = read_bits(bit_width);

        if (iterations % 10000 == 0 || iterations < 20) {
            std::cerr << "  iter=" << iterations << " bit_pos=" << bit_pos << " bit_width=" << bit_width << " dict=" << dict_size << " out=" << output.size() << " code=" << code << "\n";
        }

        if (code == 0xFFFF || code == END_CODE) {
            std::cerr << "  END_CODE or EOF at iter " << iterations << "\n";
            break;
        }

        if (code == CLEAR_CODE) {
            std::cerr << "  CLEAR_CODE at iter " << iterations << "\n";
            dict_size = FIRST_FREE;
            bit_width = 9;
            max_code = 511;
            prev_code = 0xFFFF;
            continue;
        }

        if (prev_code == 0xFFFF) {
            if (code < 256) {
                output.push_back(static_cast<uint8_t>(code));
                prev_code = code;
            }
            continue;
        }

        if (code <= dict_size) {
            // Code exists in dictionary or KwKwK pattern
            std::vector<uint8_t> stack;
            uint16_t cur = (code == dict_size) ? prev_code : code;
            bool append_first = (code == dict_size);
            while (cur >= 256) {
                if (cur >= dict_size) return std::nullopt;  // Invalid
                stack.push_back(dict[cur].ch);
                cur = dict[cur].prefix;
            }
            stack.push_back(static_cast<uint8_t>(cur));

            uint8_t first_char = static_cast<uint8_t>(cur);

            for (auto it = stack.rbegin(); it != stack.rend(); ++it) {
                output.push_back(*it);
            }
            if (append_first) {
                output.push_back(first_char);
            }

            if (dict_size < 8192) {
                dict[dict_size].prefix = prev_code;
                dict[dict_size].ch = first_char;
                dict_size++;

                if (dict_size > max_code && bit_width < 13) {
                    bit_width++;
                    max_code = (1 << bit_width) - 1;
                }
            }
        } else {
            // Invalid code
            return std::nullopt;
        }

        prev_code = code;
    }

    if (output.empty() || output.size() < 10) {
        return std::nullopt;
    }

    return output;
}

std::optional<std::vector<uint8_t>> lzp1_decompress(const uint8_t* src, size_t src_size) {
    if (!src || src_size < 32) {
        return std::nullopt;
    }

    // Check LZP1 magic at offset 0x14 (20)
    const char* magic = reinterpret_cast<const char*>(src + 0x14);
    if (magic[0] != 'L' || magic[1] != 'Z' || magic[2] != 'P' || magic[3] != '1') {
        return std::nullopt;
    }

    // Read LZP1 header:
    // 0x10 (16): compressed_size
    // 0x14 (20): "LZP1" magic
    // 0x18 (24): decompressed_size
    // 0x1C (28): flags
    // Compressed data starts at 0x20 (32)
    uint32_t compressed_size = *reinterpret_cast<const uint32_t*>(src + 0x10);
    uint32_t decompressed_size = *reinterpret_cast<const uint32_t*>(src + 0x18);
    uint32_t flags = *reinterpret_cast<const uint32_t*>(src + 0x1C);

    std::cerr << "[debug] lzp1_decompress: compressed_size=" << compressed_size
              << " decompressed_size=" << decompressed_size << " flags=" << flags << "\n";

    const uint8_t* compressed_data = src + 0x20;

    if (compressed_size == 0 || compressed_size > src_size - 0x20) {
        return std::nullopt;
    }

    size_t max_bits = compressed_size * 8;
    std::cerr << "[debug] max_bits=" << max_bits << "\n";

    // Scan for all CLEAR_CODE positions, try each one, pick the best result
    // Priority: exact match to expected size > "3DO1" magic header > largest output
    std::optional<std::vector<uint8_t>> best_result;
    size_t best_size = 0;
    size_t best_start = 0;
    bool best_msb = false;
    int found = 0;
    bool found_exact = false;

    for (int msb = 0; msb < 2; msb++) {
        bool msb_first = (msb == 1);
        std::cerr << "[debug] Trying " << (msb_first ? "MSB" : "LSB") << "-first bit order\n";

        for (size_t test_pos = 0; test_pos + 9 <= max_bits; test_pos++) {
            if (test_pos % 10000 == 0) {
                std::cerr << "[debug] Scanning bit " << test_pos << "/" << max_bits << "\n";
            }
            uint16_t code = msb_first ? read_9bits_msb(compressed_data, compressed_size, test_pos)
                                      : read_9bits_lsb(compressed_data, compressed_size, test_pos);
            if (code == 256) {  // CLEAR_CODE
                // Extra debug for known good positions
                bool debug_this = (test_pos == 3972 || test_pos == 3980 || test_pos == 3988 || test_pos == 4051 || test_pos == 4052 || test_pos == 16 || test_pos == 17);
                if (debug_this) {
                    std::cerr << "[debug] Found CLEAR at bit " << test_pos << " (" << (msb_first ? "MSB" : "LSB") << ") - KNOWN GOOD POSITION, trying...\n";
                } else {
                    std::cerr << "[debug] Found CLEAR at bit " << test_pos << " (" << (msb_first ? "MSB" : "LSB") << "), trying...\n";
                }
                auto result = try_decompress_from(compressed_data, compressed_size, test_pos, decompressed_size, msb_first);
                if (result && result->size() > 0) {
                    found++;
                    bool is_exact = (result->size() == decompressed_size);
                    bool has_3do1 = (result->size() >= 4 &&
                        (*result)[0] == '3' && (*result)[1] == 'D' &&
                        (*result)[2] == 'O' && (*result)[3] == '1');

                    // Debug early positions
                    if (test_pos < 1000 || (msb_first && test_pos < 7000 && test_pos > 6000)) {
                        std::cerr << "[debug]   Result at bit " << test_pos << " (" << (msb_first ? "MSB" : "LSB") << "): size=" << result->size() << " exact=" << is_exact << " 3do1=" << has_3do1 << "\n";
                    }
                    // Also log MSB positions around 6200
                    if (msb_first && test_pos > 6100 && test_pos < 6300) {
                        std::cerr << "[debug]   MSB Result at bit " << test_pos << ": size=" << result->size() << " exact=" << is_exact << " 3do1=" << has_3do1 << "\n";
                    }

                    // Priority: 3DO1 magic header > exact size match > larger size
                    bool update = false;
                    if (!best_result) {
                        update = true;
                    } else if (has_3do1) {
                        // Check if current best has 3DO1
                        bool best_has_3do1 = (best_result->size() >= 4 &&
                            (*best_result)[0] == '3' && (*best_result)[1] == 'D' &&
                            (*best_result)[2] == 'O' && (*best_result)[3] == '1');
                        if (!best_has_3do1) update = true;  // 3DO1 beats non-3DO1
                    } else if (is_exact && best_size != decompressed_size) {
                        // Exact match beats non-exact, non-3DO1 results
                        bool best_has_3do1 = (best_result->size() >= 4 &&
                            (*best_result)[0] == '3' && (*best_result)[1] == 'D' &&
                            (*best_result)[2] == 'O' && (*best_result)[3] == '1');
                        bool best_is_exact = (best_size == decompressed_size);
                        if (!best_has_3do1 && !best_is_exact) update = true;
                    } else if (result->size() > best_size && !has_3do1) {
                        // Larger size (fallback), but only if neither has 3DO1
                        bool best_has_3do1 = (best_result->size() >= 4 &&
                            (*best_result)[0] == '3' && (*best_result)[1] == 'D' &&
                            (*best_result)[2] == 'O' && (*best_result)[3] == '1');
                        if (!best_has_3do1) update = true;
                    }

                    if (update) {
                        best_size = result->size();
                        best_start = test_pos;
                        best_msb = msb_first;
                        best_result = std::move(result);

                        if (has_3do1) {
                            std::cerr << "[debug] Found 3DO1 header at bit " << test_pos << "!\n";
                            // Don't break - there might be a better 3DO1 result, or we want to complete the scan
                        }
                    }
                }
            }
        }
        if (found_exact) break;
    }

    if (!best_result) {
        std::cerr << "lzp1_decompress: No valid decompression found in " << max_bits << " bits\n";
        return std::nullopt;
    }

    std::cerr << "lzp1_decompress: Best CLEAR_CODE at bit " << best_start
              << " (byte " << best_start/8 << "), " << (best_msb ? "MSB" : "LSB") << "-first, output size: " << best_size << "\n";

    return best_result;
}

} // namespace c3::data