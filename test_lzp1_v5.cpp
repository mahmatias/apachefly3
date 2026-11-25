#include <vector>
#include <cstdint>
#include <iostream>
#include <fstream>
#include <iomanip>

void search_codes(const uint8_t* data, size_t size, const char* name) {
    std::cout << "\n=== " << name << " - 9-bit LSB codes ===\n";
    size_t bit_pos = 0;
    for (int i = 0; i < 100 && bit_pos + 9 <= size * 8; i++) {
        uint16_t code = 0;
        for (int b = 0; b < 9; b++) {
            size_t byte = bit_pos / 8;
            int bit = bit_pos % 8;
            code |= ((data[byte] >> bit) & 1) << b;
            bit_pos++;
        }
        if (code == 256 || code == 257) {
            std::cout << "  code[" << i << "] = " << code << " (CLEAR/END!) bit_pos=" << bit_pos << "\n";
        } else if (i < 20) {
            std::cout << "  code[" << i << "] = " << code << "\n";
        }
    }
    std::cout << "  ... total bits read: " << bit_pos << " / " << (size * 8) << "\n";
}

void try_decompress_9bit_lsb(const uint8_t* compressed_data, size_t compressed_size, uint32_t decompressed_size) {
    std::cout << "\n=== Attempting 9-bit LSB decompression ===\n";

    struct DictEntry {
        uint16_t prefix;
        uint8_t  ch;
    };

    DictEntry dict[8192];
    uint16_t dict_size = 256;

    for (uint16_t i = 0; i < 256; ++i) {
        dict[i].prefix = 0xFFFF;
        dict[i].ch = static_cast<uint8_t>(i);
    }

    const uint16_t CLEAR_CODE = 256;
    const uint16_t END_CODE = 257;
    const uint16_t FIRST_FREE = 258;

    std::vector<uint8_t> output;
    output.reserve(decompressed_size);

    size_t bit_pos = 0;
    int bit_width = 9;
    uint16_t max_code = (1 << bit_width) - 1;
    uint16_t prev_code = 0xFFFF;

    int codes_read = 0;
    while (bit_pos + bit_width <= compressed_size * 8) {
        uint16_t code = 0;
        for (int b = 0; b < bit_width; b++) {
            size_t byte = bit_pos / 8;
            int bit = bit_pos % 8;
            code |= ((compressed_data[byte] >> bit) & 1) << b;
            bit_pos++;
        }
        codes_read++;

        if (code == END_CODE) {
            std::cout << "  END_CODE at code " << codes_read << ", output size = " << output.size() << "\n";
            break;
        }

        if (code == CLEAR_CODE) {
            dict_size = FIRST_FREE;
            bit_width = 9;
            max_code = (1 << bit_width) - 1;
            prev_code = 0xFFFF;
            std::cout << "  CLEAR_CODE at code " << codes_read << ", dict reset\n";
            continue;
        }

        if (codes_read < 20 || codes_read % 1000 == 0) {
            std::cout << "  code[" << codes_read << "] = " << code
                      << " bit_width=" << bit_width
                      << " dict_size=" << dict_size
                      << " output=" << output.size() << "\n";
        }

        if (prev_code == 0xFFFF) {
            if (code < 256) {
                output.push_back(static_cast<uint8_t>(code));
                prev_code = code;
            }
            continue;
        }

        if (code < dict_size) {
            std::vector<uint8_t> stack;
            uint16_t cur = code;
            while (cur >= 256) {
                if (cur >= dict_size) {
                    std::cerr << "  Invalid cur " << cur << " >= dict_size " << dict_size << "\n";
                    return;
                }
                stack.push_back(dict[cur].ch);
                cur = dict[cur].prefix;
            }
            stack.push_back(static_cast<uint8_t>(cur));

            for (auto it = stack.rbegin(); it != stack.rend(); ++it) {
                output.push_back(*it);
            }

            if (dict_size < 8192) {
                dict[dict_size].prefix = prev_code;
                dict[dict_size].ch = static_cast<uint8_t>(cur);
                dict_size++;

                if (dict_size > max_code && bit_width < 13) {
                    bit_width++;
                    max_code = (1 << bit_width) - 1;
                }
            }
        } else if (code == dict_size) {
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
            std::cerr << "  Invalid code " << code << " >= dict_size " << dict_size << "\n";
            return;
        }

        prev_code = code;

        if (output.size() > decompressed_size + 1000) {
            std::cerr << "  Output exceeded expected size\n";
            break;
        }
    }

    std::cout << "  Final output size: " << output.size() << " (expected: " << decompressed_size << ")\n";

    // Write output
    std::ofstream out("decompressed_9bit_lsb.bin", std::ios::binary);
    out.write(reinterpret_cast<const char*>(output.data()), output.size());
    std::cout << "  Written to decompressed_9bit_lsb.bin\n";

    // Check first 32 bytes
    std::cout << "  First 32 bytes:\n";
    for (size_t i = 0; i < std::min(output.size(), (size_t)32); i++) {
        if (i % 16 == 0) std::cout << "    " << std::hex << std::setw(4) << std::setfill('0') << i << ": ";
        std::cout << std::hex << std::setw(2) << std::setfill('0') << (int)output[i] << " ";
        if ((i + 1) % 16 == 0) std::cout << "\n";
    }
    std::cout << std::dec << "\n";
}

int main() {
    // ISO file
    std::ifstream file("entry_0373", std::ios::binary);
    file.seekg(0, std::ios::end);
    size_t size = file.tellg();
    file.seekg(0, std::ios::beg);
    std::vector<uint8_t> data(size);
    file.read(reinterpret_cast<char*>(data.data()), size);

    uint32_t compressed_size = *reinterpret_cast<uint32_t*>(data.data() + 0x10);
    uint32_t decompressed_size = *reinterpret_cast<uint32_t*>(data.data() + 0x18);
    const uint8_t* compressed_data = data.data() + 0x20;

    std::cout << "ISO entry_0373: compressed=" << compressed_size << " decompressed=" << decompressed_size << "\n";
    search_codes(compressed_data, compressed_size, "ISO");

    // Installed file
    std::ifstream file2("entry_1685", std::ios::binary);
    file2.seekg(0, std::ios::end);
    size_t size2 = file2.tellg();
    file2.seekg(0, std::ios::beg);
    std::vector<uint8_t> data2(size2);
    file2.read(reinterpret_cast<char*>(data2.data()), size2);

    uint32_t compressed_size2 = *reinterpret_cast<uint32_t*>(data2.data() + 0x10);
    uint32_t decompressed_size2 = *reinterpret_cast<uint32_t*>(data2.data() + 0x18);
    const uint8_t* compressed_data2 = data2.data() + 0x20;

    std::cout << "\nInstalled entry_1685: compressed=" << compressed_size2 << " decompressed=" << decompressed_size2 << "\n";
    search_codes(compressed_data2, compressed_size2, "Installed");

    // Try decompression on installed file (has valid CLEAR/END codes)
    try_decompress_9bit_lsb(compressed_data2, compressed_size2, decompressed_size2);

    return 0;
}