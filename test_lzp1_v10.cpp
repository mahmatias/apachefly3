#include <vector>
#include <cstdint>
#include <iostream>
#include <fstream>
#include <iomanip>

void try_byte_aligned_codes(const uint8_t* data, size_t size, const char* name) {
    std::cout << "\n=== " << name << " - Byte-aligned code attempts ===\n";

    // Try 12-bit codes packed as 3 bytes = 2 codes (12+12=24 bits)
    std::cout << "\n  12-bit codes (3 bytes = 2 codes):\n";
    for (size_t i = 0; i + 2 < size; i += 3) {
        uint16_t code1 = data[i] | ((data[i+1] & 0x0F) << 8);
        uint16_t code2 = (data[i+1] >> 4) | (data[i+2] << 4);
        if (i < 30) {
            std::cout << "    [" << (i/3)*2 << "] = " << code1 << ", [" << (i/3)*2+1 << "] = " << code2 << "\n";
        }
        if (code1 == 256 || code1 == 257) {
            std::cout << "    CLEAR/END at index " << (i/3)*2 << " (byte " << i << "): " << code1 << "\n";
        }
        if (code2 == 256 || code2 == 257) {
            std::cout << "    CLEAR/END at index " << (i/3)*2+1 << " (byte " << i+1 << "): " << code2 << "\n";
        }
    }

    // Try 16-bit LE codes
    std::cout << "\n  16-bit LE codes:\n";
    for (size_t i = 0; i + 1 < size; i += 2) {
        uint16_t code = data[i] | (data[i+1] << 8);
        if (i < 30) {
            std::cout << "    [" << (i/2) << "] = " << code << " (0x" << std::hex << code << std::dec << ")\n";
        }
        if (code == 256 || code == 257) {
            std::cout << "    CLEAR/END at index " << (i/2) << " (byte " << i << "): " << code << "\n";
        }
    }

    // Try 16-bit BE codes
    std::cout << "\n  16-bit BE codes:\n";
    for (size_t i = 0; i + 1 < size; i += 2) {
        uint16_t code = (data[i] << 8) | data[i+1];
        if (i < 30) {
            std::cout << "    [" << (i/2) << "] = " << code << " (0x" << std::hex << code << std::dec << ")\n";
        }
        if (code == 256 || code == 257) {
            std::cout << "    CLEAR/END at index " << (i/2) << " (byte " << i << "): " << code << "\n";
        }
    }

    // Try 9-bit codes but starting from different byte alignments
    for (int align = 0; align < 8; align++) {
        std::cout << "\n  9-bit LSB, bit alignment " << align << ":\n";
        size_t bit_pos = align;
        bool found_clear = false;
        for (int c = 0; c < 100 && bit_pos + 9 <= size * 8; c++) {
            uint16_t code = 0;
            for (int b = 0; b < 9; b++) {
                size_t byte = bit_pos / 8;
                int bit = bit_pos % 8;
                if (byte >= size) break;
                code |= ((data[byte] >> bit) & 1) << b;
                bit_pos++;
            }
            if (code == 256 || code == 257) {
                std::cout << "    code[" << c << "] = " << code << " at bit_pos=" << bit_pos << "\n";
                found_clear = true;
            }
        }
        if (!found_clear) {
            std::cout << "    No CLEAR/END in first 100 codes\n";
        }
    }
}

void decompress_16bit_le(const uint8_t* data, size_t size, uint32_t expected_size, const char* label) {
    std::cout << "\n=== " << label << " - 16-bit LE decompression ===\n";

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
    output.reserve(expected_size);

    uint16_t prev_code = 0xFFFF;
    bool in_data = false;

    for (size_t i = 0; i + 1 < size; i += 2) {
        uint16_t code = data[i] | (data[i+1] << 8);

        if (code == END_CODE) {
            std::cout << "  END_CODE at index " << (i/2) << ", output size = " << output.size() << "\n";
            break;
        }

        if (code == CLEAR_CODE) {
            dict_size = FIRST_FREE;
            prev_code = 0xFFFF;
            in_data = true;
            std::cout << "  CLEAR_CODE at index " << (i/2) << ", dict reset\n";
            continue;
        }

        if (!in_data) continue;

        if (i/2 < 20 || (i/2) % 1000 == 0) {
            std::cout << "  code[" << (i/2) << "] = " << code
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
            }
        } else {
            std::cerr << "  Invalid code " << code << " >= dict_size " << dict_size << "\n";
            return;
        }

        prev_code = code;

        if (output.size() > expected_size + 1000) {
            std::cerr << "  Output exceeded expected size\n";
            break;
        }
    }

    std::cout << "  Final output size: " << output.size() << " (expected: " << expected_size << ")\n";

    std::string fname = std::string("decompressed_") + label + ".bin";
    std::ofstream out(fname, std::ios::binary);
    out.write(reinterpret_cast<const char*>(output.data()), output.size());
    std::cout << "  Written to " << fname << "\n";
}

int main() {
    // Installed file - has valid CLEAR/END codes
    std::ifstream file2("entry_1685", std::ios::binary);
    file2.seekg(0, std::ios::end);
    size_t size2 = file2.tellg();
    file2.seekg(0, std::ios::beg);
    std::vector<uint8_t> data2(size2);
    file2.read(reinterpret_cast<char*>(data2.data()), size2);

    uint32_t compressed_size2 = *reinterpret_cast<uint32_t*>(data2.data() + 0x10);
    uint32_t decompressed_size2 = *reinterpret_cast<uint32_t*>(data2.data() + 0x18);
    const uint8_t* compressed_data2 = data2.data() + 0x20;

    std::cout << "Installed entry_1685: compressed=" << compressed_size2 << " decompressed=" << decompressed_size2 << "\n";

    try_byte_aligned_codes(compressed_data2, compressed_size2, "Installed");

    // Try 16-bit LE decompression from start of compressed data
    decompress_16bit_le(compressed_data2, compressed_size2, decompressed_size2, "inst_16le");

    // Also try from first CLEAR code position
    // First CLEAR at offset 0x61b in file = 0x61b - 0x20 = 0x5fb in compressed_data
    size_t clear_offset = 0x5fb;
    if (clear_offset % 2 == 0) {
        decompress_16bit_le(compressed_data2 + clear_offset, compressed_size2 - clear_offset, decompressed_size2, "inst_16le_from_clear");
    }

    return 0;
}