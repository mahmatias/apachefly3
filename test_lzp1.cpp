#include <vector>
#include <cstdint>
#include <iostream>
#include <fstream>
#include <optional>

std::optional<std::vector<uint8_t>> lzp1_decompress_debug(const uint8_t* src, size_t src_size) {
    if (!src || src_size < 32) {
        std::cerr << "Input too small\n";
        return std::nullopt;
    }

    // Check LZP1 magic at offset 0x14 (20)
    const char* magic = reinterpret_cast<const char*>(src + 0x14);
    if (magic[0] != 'L' || magic[1] != 'Z' || magic[2] != 'P' || magic[3] != '1') {
        std::cerr << "Invalid magic\n";
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

    std::cout << "Header: compressed_size=" << compressed_size
              << " decompressed_size=" << decompressed_size
              << " flags=" << flags << std::endl;

    const uint8_t* compressed_data = src + 0x20;

    if (compressed_size == 0 || compressed_size > src_size - 0x20) {
        std::cerr << "Invalid compressed_size\n";
        return std::nullopt;
    }

    // LZP1 uses GIF-style LZW with variable bit width (9-13 bits)
    // Dictionary size: 8192 entries (2^13)

    std::vector<uint8_t> output;
    output.reserve(decompressed_size);

    // LZW dictionary: 8192 entries, each entry is a (prefix, char) pair
    // Initial dictionary: 256 single-byte entries (0-255)
    struct DictEntry {
        uint16_t prefix;
        uint8_t  ch;
    };

    DictEntry dict[8192];
    uint16_t dict_size = 256;

    // Initialize dictionary with single-byte entries
    for (uint16_t i = 0; i < 256; ++i) {
        dict[i].prefix = 0xFFFF;  // No prefix
        dict[i].ch = static_cast<uint8_t>(i);
    }

    // Special codes
    const uint16_t CLEAR_CODE = 256;
    const uint16_t END_CODE = 257;
    const uint16_t FIRST_FREE = 258;

    // Try LSB-first bit stream reading (standard LZW)
    size_t bit_pos = 0;
    auto read_bits_lsb = [&](int n) -> uint16_t {
        if (bit_pos + n > compressed_size * 8) return 0;
        size_t byte_pos = bit_pos / 8;
        int bit_offset = bit_pos % 8;
        uint32_t val = 0;
        int remaining = n;
        while (remaining > 0) {
            int bits_this_byte = std::min(remaining, 8 - bit_offset);
            // LSB-first: read from bit_offset upwards
            uint32_t mask = (1u << bits_this_byte) - 1;
            val |= ((compressed_data[byte_pos] >> bit_offset) & mask) << (n - remaining);
            remaining -= bits_this_byte;
            byte_pos++;
            bit_offset = 0;
        }
        bit_pos += n;
        return static_cast<uint16_t>(val);
    };

    // Also try MSB-first
    size_t bit_pos_msb = 0;
    auto read_bits_msb = [&](int n) -> uint16_t {
        if (bit_pos_msb + n > compressed_size * 8) return 0;
        size_t byte_pos = bit_pos_msb / 8;
        int bit_offset = bit_pos_msb % 8;
        uint32_t val = 0;
        int remaining = n;
        while (remaining > 0) {
            int bits_this_byte = std::min(remaining, 8 - bit_offset);
            // MSB-first: read from bit (7 - bit_offset) downwards
            uint32_t mask = ((1u << bits_this_byte) - 1) << (8 - bit_offset - bits_this_byte);
            val = (val << bits_this_byte) | ((compressed_data[byte_pos] & mask) >> (8 - bit_offset - bits_this_byte));
            remaining -= bits_this_byte;
            byte_pos++;
            bit_offset = 0;
        }
        bit_pos_msb += n;
        return static_cast<uint16_t>(val);
    };

    auto try_decompress = [&](auto read_bits, const char* mode) -> std::optional<std::vector<uint8_t>> {
        // Reset dictionary
        DictEntry local_dict[8192];
        uint16_t local_dict_size = 256;
        for (uint16_t i = 0; i < 256; ++i) {
            local_dict[i].prefix = 0xFFFF;
            local_dict[i].ch = static_cast<uint8_t>(i);
        }

        std::vector<uint8_t> local_output;
        local_output.reserve(decompressed_size);

        int bit_width = 9;
        uint16_t max_code = (1 << bit_width) - 1;
        uint16_t prev_code = 0xFFFF;

        // Reset bit position
        if (mode[0] == 'L') bit_pos = 0; else bit_pos_msb = 0;

        int codes_read = 0;
        while ((mode[0] == 'L' ? bit_pos : bit_pos_msb) < compressed_size * 8) {
            uint16_t code = read_bits(bit_width);
            codes_read++;

            if (code == END_CODE) {
                std::cout << mode << ": END_CODE at code " << codes_read << std::endl;
                break;
            }

            if (code == CLEAR_CODE) {
                local_dict_size = FIRST_FREE;
                bit_width = 9;
                max_code = (1 << bit_width) - 1;
                prev_code = 0xFFFF;
                std::cout << mode << ": CLEAR_CODE at code " << codes_read << std::endl;
                continue;
            }

            if (codes_read < 10 || codes_read % 1000 == 0) {
                std::cout << mode << ": code=" << code << " bit_width=" << bit_width
                          << " dict_size=" << local_dict_size << " output_size=" << local_output.size() << std::endl;
            }

            if (prev_code == 0xFFFF) {
                // First code after clear
                if (code < 256) {
                    local_output.push_back(static_cast<uint8_t>(code));
                    prev_code = code;
                }
                continue;
            }

            if (code < local_dict_size) {
                // Code exists in dictionary
                std::vector<uint8_t> stack;
                uint16_t cur = code;
                while (cur >= 256) {
                    if (cur >= local_dict_size) {
                        std::cerr << mode << ": Invalid cur " << cur << " >= dict_size " << local_dict_size << std::endl;
                        return std::nullopt;
                    }
                    stack.push_back(local_dict[cur].ch);
                    cur = local_dict[cur].prefix;
                }
                stack.push_back(static_cast<uint8_t>(cur));

                for (auto it = stack.rbegin(); it != stack.rend(); ++it) {
                    local_output.push_back(*it);
                }

                // Add new entry to dictionary
                if (local_dict_size < 8192) {
                    local_dict[local_dict_size].prefix = prev_code;
                    local_dict[local_dict_size].ch = static_cast<uint8_t>(cur);
                    local_dict_size++;

                    // Increase bit width if needed
                    if (local_dict_size > max_code && bit_width < 13) {
                        bit_width++;
                        max_code = (1 << bit_width) - 1;
                    }
                }
            } else if (code == local_dict_size) {
                // Special case: code is the next to be added (KwKwK pattern)
                std::vector<uint8_t> stack;
                uint16_t cur = prev_code;
                while (cur >= 256) {
                    stack.push_back(local_dict[cur].ch);
                    cur = local_dict[cur].prefix;
                }
                stack.push_back(static_cast<uint8_t>(cur));

                uint8_t first_char = static_cast<uint8_t>(cur);

                for (auto it = stack.rbegin(); it != stack.rend(); ++it) {
                    local_output.push_back(*it);
                }
                local_output.push_back(first_char);

                // Add new entry
                if (local_dict_size < 8192) {
                    local_dict[local_dict_size].prefix = prev_code;
                    local_dict[local_dict_size].ch = first_char;
                    local_dict_size++;

                    if (local_dict_size > max_code && bit_width < 13) {
                        bit_width++;
                        max_code = (1 << bit_width) - 1;
                    }
                }
            } else {
                std::cerr << mode << ": Invalid code " << code << " >= dict_size " << local_dict_size << std::endl;
                return std::nullopt;
            }

            prev_code = code;

            if (local_output.size() > decompressed_size + 1000) {
                std::cerr << mode << ": Output exceeded expected size\n";
                break;
            }
        }

        std::cout << mode << ": Final output size=" << local_output.size()
                  << " expected=" << decompressed_size << " codes_read=" << codes_read << std::endl;
        return local_output;
    };

    // Try LSB-first
    std::cout << "\n=== Trying LSB-first ===\n";
    auto result_lsb = try_decompress(read_bits_lsb, "LSB");

    // Try MSB-first
    std::cout << "\n=== Trying MSB-first ===\n";
    auto result_msb = try_decompress(read_bits_msb, "MSB");

    if (result_lsb && result_lsb->size() == decompressed_size) {
        return result_lsb;
    }
    if (result_msb && result_msb->size() == decompressed_size) {
        return result_msb;
    }
    if (result_lsb) {
        return result_lsb;
    }
    if (result_msb) {
        return result_msb;
    }

    return std::nullopt;
}

int main() {
    std::ifstream file("entry_0373", std::ios::binary);
    if (!file) {
        std::cerr << "Failed to open file\n";
        return 1;
    }
    file.seekg(0, std::ios::end);
    size_t size = file.tellg();
    file.seekg(0, std::ios::beg);
    std::vector<uint8_t> data(size);
    file.read(reinterpret_cast<char*>(data.data()), size);

    auto result = lzp1_decompress_debug(data.data(), data.size());
    if (result) {
        std::ofstream out("entry_0373.decompressed", std::ios::binary);
        out.write(reinterpret_cast<const char*>(result->data()), result->size());
        std::cout << "Decompressed to entry_0373.decompressed (" << result->size() << " bytes)\n";
    } else {
        std::cerr << "Decompression failed\n";
    }
    return 0;
}