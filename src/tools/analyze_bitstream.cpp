#include <vector>
#include <cstdint>
#include <iostream>
#include <iomanip>
#include <fstream>
#include <string>
#include <optional>

void trace_bitstream(const uint8_t* data, size_t size, size_t start_bit, int bit_width, int max_codes, const char* label) {
    std::cout << "\n=== " << label << " ===\n";
    size_t bit_pos = start_bit;
    for (int i = 0; i < max_codes && bit_pos + bit_width <= size * 8; i++) {
        uint16_t code = 0;
        for (int b = 0; b < bit_width; b++) {
            size_t byte = bit_pos / 8;
            int bit = bit_pos % 8;
            if (byte >= size) break;
            code |= ((data[byte] >> bit) & 1) << b;
            bit_pos++;
        }
        const char* special = "";
        if (code == 256) special = " <-- CLEAR";
        else if (code == 257) special = " <-- END";
        else if (code < 256) special = " <-- LITERAL";
        std::cout << "  code[" << i << "] = " << code << special << "\n";
    }
}

void trace_bitstream_msb(const uint8_t* data, size_t size, size_t start_bit, int bit_width, int max_codes, const char* label) {
    std::cout << "\n=== " << label << " (MSB-first) ===\n";
    size_t bit_pos = start_bit;
    for (int i = 0; i < max_codes && bit_pos + bit_width <= size * 8; i++) {
        uint16_t code = 0;
        for (int b = 0; b < bit_width; b++) {
            size_t byte = bit_pos / 8;
            int bit = 7 - (bit_pos % 8);  // MSB first
            if (byte >= size) break;
            code |= ((data[byte] >> bit) & 1) << (bit_width - 1 - b);
            bit_pos++;
        }
        const char* special = "";
        if (code == 256) special = " <-- CLEAR";
        else if (code == 257) special = " <-- END";
        else if (code < 256) special = " <-- LITERAL";
        std::cout << "  code[" << i << "] = " << code << special << "\n";
    }
}

// Test decompression at specific alignment (LSB-first)
std::optional<std::vector<uint8_t>> test_decompress_lsb(const uint8_t* compressed_data, size_t compressed_size, size_t data_start_bit, int start_bit_width, bool debug = false) {
    // LZW dictionary: 8192 entries
    struct DictEntry {
        uint16_t prefix;
        uint8_t  ch;
    };

    DictEntry dict[8192];
    uint16_t dict_size = 256;

    // Initialize dictionary with single-byte entries
    for (uint16_t i = 0; i < 256; ++i) {
        dict[i].prefix = 0xFFFF;
        dict[i].ch = static_cast<uint8_t>(i);
    }

    const uint16_t CLEAR_CODE = 256;
    const uint16_t END_CODE = 257;
    const uint16_t FIRST_FREE = 258;

    size_t bit_pos = data_start_bit;

    auto read_bits = [&](int n) -> uint16_t {
        if (bit_pos + n > compressed_size * 8) return 0;
        uint32_t val = 0;
        for (int b = 0; b < n; b++) {
            size_t byte = bit_pos / 8;
            int bit = bit_pos % 8;
            if (byte >= compressed_size) break;
            val |= ((compressed_data[byte] >> bit) & 1) << b;
            bit_pos++;
        }
        return static_cast<uint16_t>(val);
    };

    int bit_width = start_bit_width;
    uint16_t max_code = (1 << bit_width) - 1;
    uint16_t prev_code = 0xFFFF;

    std::vector<uint8_t> output;

    int code_count = 0;
    while (bit_pos < compressed_size * 8) {
        uint16_t code = read_bits(bit_width);

        if (debug && code_count < 30) {
            std::cout << "  Read code[" << code_count << "] = " << code
                      << " (bit_width=" << bit_width << ", dict_size=" << dict_size
                      << ", max_code=" << max_code << ", bit_pos=" << bit_pos << ")\n";
        }

        if (code == END_CODE) {
            if (debug) std::cout << "  END_CODE\n";
            break;
        }

        if (code == CLEAR_CODE) {
            if (debug) std::cout << "  CLEAR_CODE\n";
            dict_size = FIRST_FREE;
            bit_width = 9;
            max_code = (1 << bit_width) - 1;
            prev_code = 0xFFFF;
            continue;
        }

        if (prev_code == 0xFFFF) {
            if (code < 256) {
                output.push_back(static_cast<uint8_t>(code));
                prev_code = code;
                if (debug) std::cout << "  First literal: " << (int)code << "\n";
            } else {
                if (debug) std::cout << "  Invalid first code: " << code << "\n";
            }
            continue;
        }

        if (code < dict_size) {
            // Code exists in dictionary
            std::vector<uint8_t> stack;
            uint16_t cur = code;
            while (cur >= 256) {
                stack.push_back(dict[cur].ch);
                cur = dict[cur].prefix;
            }
            stack.push_back(static_cast<uint8_t>(cur));

            for (auto it = stack.rbegin(); it != stack.rend(); ++it) {
                output.push_back(*it);
            }

            // Add new entry to dictionary
            if (dict_size < 8192) {
                dict[dict_size].prefix = prev_code;
                dict[dict_size].ch = static_cast<uint8_t>(cur);
                dict_size++;

                if (dict_size > max_code && bit_width < 13) {
                    bit_width++;
                    max_code = (1 << bit_width) - 1;
                    if (debug) std::cout << "  Increased bit_width to " << bit_width << "\n";
                }
            }
        } else if (code == dict_size) {
            // Special case: KwKwK pattern
            std::vector<uint8_t> stack;
            uint16_t cur = prev_code;
            while (cur >= 256) {
                stack.push_back(dict[cur].ch);
                cur = dict[cur].prefix;
            }
            stack.push_back(static_cast<uint8_t>(cur));

            uint8_t first_char = static_cast<uint8_t>(cur);

            for (auto it = stack.rbegin(); it != stack.rend(); ++it) {
                output.push_back(*it);
            }
            output.push_back(first_char);

            // Add new entry
            if (dict_size < 8192) {
                dict[dict_size].prefix = prev_code;
                dict[dict_size].ch = first_char;
                dict_size++;

                if (dict_size > max_code && bit_width < 13) {
                    bit_width++;
                    max_code = (1 << bit_width) - 1;
                    if (debug) std::cout << "  Increased bit_width to " << bit_width << "\n";
                }
            }
        } else {
            if (debug) std::cout << "  Invalid code: " << code << " (dict_size=" << dict_size << ", max_code=" << max_code << ")\n";
            return std::nullopt;
        }

        prev_code = code;
        code_count++;
    }

    return output;
}

// Test decompression at specific alignment (MSB-first)
std::optional<std::vector<uint8_t>> test_decompress_msb(const uint8_t* compressed_data, size_t compressed_size, size_t data_start_bit, int start_bit_width, bool debug = false) {
    // LZW dictionary: 8192 entries
    struct DictEntry {
        uint16_t prefix;
        uint8_t  ch;
    };

    DictEntry dict[8192];
    uint16_t dict_size = 256;

    // Initialize dictionary with single-byte entries
    for (uint16_t i = 0; i < 256; ++i) {
        dict[i].prefix = 0xFFFF;
        dict[i].ch = static_cast<uint8_t>(i);
    }

    const uint16_t CLEAR_CODE = 256;
    const uint16_t END_CODE = 257;
    const uint16_t FIRST_FREE = 258;

    size_t bit_pos = data_start_bit;

    auto read_bits = [&](int n) -> uint16_t {
        if (bit_pos + n > compressed_size * 8) return 0;
        uint32_t val = 0;
        for (int b = 0; b < n; b++) {
            size_t byte = bit_pos / 8;
            int bit = 7 - (bit_pos % 8);  // MSB first
            if (byte >= compressed_size) break;
            val |= ((compressed_data[byte] >> bit) & 1) << (n - 1 - b);
            bit_pos++;
        }
        return static_cast<uint16_t>(val);
    };

    int bit_width = start_bit_width;
    uint16_t max_code = (1 << bit_width) - 1;
    uint16_t prev_code = 0xFFFF;

    std::vector<uint8_t> output;

    int code_count = 0;
    while (bit_pos < compressed_size * 8) {
        uint16_t code = read_bits(bit_width);

        if (debug && code_count < 30) {
            std::cout << "  Read code[" << code_count << "] = " << code
                      << " (bit_width=" << bit_width << ", dict_size=" << dict_size
                      << ", max_code=" << max_code << ", bit_pos=" << bit_pos << ")\n";
        }

        if (code == END_CODE) {
            if (debug) std::cout << "  END_CODE\n";
            break;
        }

        if (code == CLEAR_CODE) {
            if (debug) std::cout << "  CLEAR_CODE\n";
            dict_size = FIRST_FREE;
            bit_width = 9;
            max_code = (1 << bit_width) - 1;
            prev_code = 0xFFFF;
            continue;
        }

        if (prev_code == 0xFFFF) {
            if (code < 256) {
                output.push_back(static_cast<uint8_t>(code));
                prev_code = code;
                if (debug) std::cout << "  First literal: " << (int)code << "\n";
            } else {
                if (debug) std::cout << "  Invalid first code: " << code << "\n";
            }
            continue;
        }

        if (code < dict_size) {
            // Code exists in dictionary
            std::vector<uint8_t> stack;
            uint16_t cur = code;
            while (cur >= 256) {
                stack.push_back(dict[cur].ch);
                cur = dict[cur].prefix;
            }
            stack.push_back(static_cast<uint8_t>(cur));

            for (auto it = stack.rbegin(); it != stack.rend(); ++it) {
                output.push_back(*it);
            }

            // Add new entry to dictionary
            if (dict_size < 8192) {
                dict[dict_size].prefix = prev_code;
                dict[dict_size].ch = static_cast<uint8_t>(cur);
                dict_size++;

                if (dict_size > max_code && bit_width < 13) {
                    bit_width++;
                    max_code = (1 << bit_width) - 1;
                    if (debug) std::cout << "  Increased bit_width to " << bit_width << "\n";
                }
            }
        } else if (code == dict_size) {
            // Special case: KwKwK pattern
            std::vector<uint8_t> stack;
            uint16_t cur = prev_code;
            while (cur >= 256) {
                stack.push_back(dict[cur].ch);
                cur = dict[cur].prefix;
            }
            stack.push_back(static_cast<uint8_t>(cur));

            uint8_t first_char = static_cast<uint8_t>(cur);

            for (auto it = stack.rbegin(); it != stack.rend(); ++it) {
                output.push_back(*it);
            }
            output.push_back(first_char);

            // Add new entry
            if (dict_size < 8192) {
                dict[dict_size].prefix = prev_code;
                dict[dict_size].ch = first_char;
                dict_size++;

                if (dict_size > max_code && bit_width < 13) {
                    bit_width++;
                    max_code = (1 << bit_width) - 1;
                    if (debug) std::cout << "  Increased bit_width to " << bit_width << "\n";
                }
            }
        } else {
            if (debug) std::cout << "  Invalid code: " << code << " (dict_size=" << dict_size << ", max_code=" << max_code << ")\n";
            return std::nullopt;
        }

        prev_code = code;
        code_count++;
    }

    return output;
}

// Test different bit widths
void test_all_bitwidths(const uint8_t* compressed_data, size_t compressed_size, size_t data_start_bit) {
    for (int bw = 9; bw <= 12; bw++) {
        std::cout << "\n=== Testing LSB bit_width=" << bw << " ===\n";
        auto result = test_decompress_lsb(compressed_data, compressed_size, data_start_bit, bw, true);
        if (result) {
            std::cout << "SUCCESS: Decompressed " << result->size() << " bytes\n";
            return;
        }
    }
    for (int bw = 9; bw <= 12; bw++) {
        std::cout << "\n=== Testing MSB bit_width=" << bw << " ===\n";
        auto result = test_decompress_msb(compressed_data, compressed_size, data_start_bit, bw, true);
        if (result) {
            std::cout << "SUCCESS: Decompressed " << result->size() << " bytes\n";
            return;
        }
    }
}

// Find alignment where first code is CLEAR
void find_clear_start(const uint8_t* compressed_data, size_t compressed_size) {
    std::cout << "\n=== Finding alignment where first code is CLEAR ===\n";
    for (int align = 0; align < 8; align++) {
        size_t test_pos = align;
        uint16_t code = 0;
        for (int b = 0; b < 9; b++) {
            size_t byte = test_pos / 8;
            int bit = test_pos % 8;
            if (byte >= compressed_size) break;
            code |= ((compressed_data[byte] >> bit) & 1) << b;
            test_pos++;
        }
        std::cout << "  Align " << align << ": first code = " << code << (code == 256 ? " <-- CLEAR!" : "") << "\n";
    }

    // Also try MSB-first
    std::cout << "\n=== MSB-first ===\n";
    for (int align = 0; align < 8; align++) {
        size_t test_pos = align;
        uint16_t code = 0;
        for (int b = 0; b < 9; b++) {
            size_t byte = test_pos / 8;
            int bit = 7 - (test_pos % 8);
            if (byte >= compressed_size) break;
            code |= ((compressed_data[byte] >> bit) & 1) << (8 - b);
            test_pos++;
        }
        std::cout << "  Align " << align << ": first code = " << code << (code == 256 ? " <-- CLEAR!" : "") << "\n";
    }

    // Scan for CLEAR in first 5000 bits (LSB)
    std::cout << "\n=== Scanning for CLEAR in first 5000 bits (LSB) ===\n";
    for (size_t bit_pos = 0; bit_pos < 5000 && bit_pos + 9 <= compressed_size * 8; bit_pos++) {
        uint16_t code = 0;
        for (int b = 0; b < 9; b++) {
            size_t byte = (bit_pos + b) / 8;
            int bit = (bit_pos + b) % 8;
            if (byte >= compressed_size) break;
            code |= ((compressed_data[byte] >> bit) & 1) << b;
        }
        if (code == 256) {
            std::cout << "  CLEAR at bit_pos=" << bit_pos << " (byte=" << bit_pos/8 << ", bit=" << bit_pos%8 << ")\n";
        }
    }

    // Scan for CLEAR in first 5000 bits (MSB)
    std::cout << "\n=== Scanning for CLEAR in first 5000 bits (MSB) ===\n";
    for (size_t bit_pos = 0; bit_pos < 5000 && bit_pos + 9 <= compressed_size * 8; bit_pos++) {
        uint16_t code = 0;
        for (int b = 0; b < 9; b++) {
            size_t byte = (bit_pos + b) / 8;
            int bit = 7 - ((bit_pos + b) % 8);
            if (byte >= compressed_size) break;
            code |= ((compressed_data[byte] >> bit) & 1) << (8 - b);
        }
        if (code == 256) {
            std::cout << "  CLEAR at bit_pos=" << bit_pos << " (byte=" << bit_pos/8 << ", bit=" << bit_pos%8 << ")\n";
        }
    }
}

int main() {
    std::ifstream file("entry_1685", std::ios::binary);
    file.seekg(0, std::ios::end);
    size_t size = file.tellg();
    file.seekg(0, std::ios::beg);
    std::vector<uint8_t> data(size);
    file.read(reinterpret_cast<char*>(data.data()), size);

    uint32_t compressed_size = *reinterpret_cast<uint32_t*>(data.data() + 0x10);
    const uint8_t* compressed_data = data.data() + 0x20;

    std::cout << "entry_1685: compressed=" << compressed_size << "\n";

    // Trace from different alignments (LSB-first)
    for (int align = 0; align < 8; align++) {
        std::string label = "9-bit align " + std::to_string(align);
        trace_bitstream(compressed_data, compressed_size, align, 9, 30, label.c_str());
    }

    // Trace MSB-first
    for (int align = 0; align < 8; align++) {
        std::string label = "9-bit MSB align " + std::to_string(align);
        trace_bitstream_msb(compressed_data, compressed_size, align, 9, 30, label.c_str());
    }

    // Find alignment where first code is CLEAR (full scan)
    find_clear_start(compressed_data, compressed_size);

    // Test decompression at align 1 (where CLEAR was at code[5])
    // CLEAR at code[5] means bit_pos = align + 5*9 = 1 + 45 = 46
    // Start AT the CLEAR code so the decompressor handles it
    std::cout << "\n=== Testing decompression at align 1 (starting AT CLEAR at bit_pos=46) ===\n";
    size_t test_start = 1 + 5 * 9;  // At CLEAR code
    test_all_bitwidths(compressed_data, compressed_size, test_start);

    // Also try starting a bit earlier to see if there's an earlier CLEAR
    std::cout << "\n=== Testing decompression at align 1 (starting at bit_pos=40) ===\n";
    test_start = 40;
    test_all_bitwidths(compressed_data, compressed_size, test_start);

    // Test full decompression from first CLEAR (bit_pos=46) with LSB 9-bit, no early exit
    std::cout << "\n=== Full LSB decompression from first CLEAR (bit_pos=46) ===\n";
    auto result = test_decompress_lsb(compressed_data, compressed_size, 46, 9, true);
    if (result) {
        std::cout << "SUCCESS: Decompressed " << result->size() << " bytes\n";
        std::cout << "First 128 bytes:\n";
        for (size_t i = 0; i < std::min(result->size(), (size_t)128); i++) {
            if (i % 16 == 0) std::cout << "  " << std::hex << std::setw(4) << std::setfill('0') << i << ": ";
            std::cout << std::hex << std::setw(2) << std::setfill('0') << (int)(*result)[i] << " ";
            if ((i + 1) % 16 == 0) std::cout << "\n";
        }
        std::cout << std::dec << "\n";

        std::ofstream out("decompressed_full.bin", std::ios::binary);
        out.write(reinterpret_cast<const char*>(result->data()), result->size());
        std::cout << "Written to decompressed_full.bin\n";
    } else {
        std::cout << "Decompression failed\n";
    }

    // Test MSB decompression from MSB CLEAR position (bit_pos=57)
    std::cout << "\n=== Full MSB decompression from first MSB CLEAR (bit_pos=57) ===\n";
    result = test_decompress_msb(compressed_data, compressed_size, 57, 9, true);
    if (result) {
        std::cout << "SUCCESS: Decompressed " << result->size() << " bytes\n";
        std::cout << "First 128 bytes:\n";
        for (size_t i = 0; i < std::min(result->size(), (size_t)128); i++) {
            if (i % 16 == 0) std::cout << "  " << std::hex << std::setw(4) << std::setfill('0') << i << ": ";
            std::cout << std::hex << std::setw(2) << std::setfill('0') << (int)(*result)[i] << " ";
            if ((i + 1) % 16 == 0) std::cout << "\n";
        }
        std::cout << std::dec << "\n";

        std::ofstream out("decompressed_full_msb.bin", std::ios::binary);
        out.write(reinterpret_cast<const char*>(result->data()), result->size());
        std::cout << "Written to decompressed_full_msb.bin\n";
    } else {
        std::cout << "MSB Decompression failed\n";
    }

    return 0;
}