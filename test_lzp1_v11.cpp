#include <vector>
#include <cstdint>
#include <iostream>
#include <fstream>
#include <iomanip>

void decompress_12bit_packed(const uint8_t* data, size_t size, uint32_t expected_size, const char* label) {
    std::cout << "\n=== " << label << " - 12-bit packed (3 bytes = 2 codes) decompression ===\n";

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

    for (size_t i = 0; i + 2 < size; i += 3) {
        uint16_t code1 = data[i] | ((data[i+1] & 0x0F) << 8);
        uint16_t code2 = (data[i+1] >> 4) | (data[i+2] << 4);

        for (int code_idx = 0; code_idx < 2; code_idx++) {
            uint16_t code = (code_idx == 0) ? code1 : code2;
            size_t code_num = (i / 3) * 2 + code_idx;

            if (code == END_CODE) {
                std::cout << "  END_CODE at code " << code_num << ", output size = " << output.size() << "\n";
                goto done;
            }

            if (code == CLEAR_CODE) {
                dict_size = FIRST_FREE;
                prev_code = 0xFFFF;
                in_data = true;
                if (code_num < 20 || code_num % 1000 == 0) {
                    std::cout << "  CLEAR_CODE at code " << code_num << ", dict reset\n";
                }
                continue;
            }

            if (!in_data) continue;

            if (code_num < 20 || code_num % 1000 == 0) {
                std::cout << "  code[" << code_num << "] = " << code
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
                        goto done;
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
                std::cerr << "  Invalid code " << code << " >= dict_size " << dict_size << " at code_num=" << code_num << "\n";
                goto done;
            }

            prev_code = code;

            if (output.size() > expected_size + 1000) {
                std::cerr << "  Output exceeded expected size\n";
                goto done;
            }
        }
    }

done:
    std::cout << "  Final output size: " << output.size() << " (expected: " << expected_size << ")\n";

    std::string fname = std::string("decompressed_") + label + ".bin";
    std::ofstream out(fname, std::ios::binary);
    out.write(reinterpret_cast<const char*>(output.data()), output.size());
    std::cout << "  Written to " << fname << "\n";

    if (!output.empty()) {
        std::cout << "  First 32 bytes:\n";
        for (size_t i = 0; i < std::min(output.size(), (size_t)32); i++) {
            if (i % 16 == 0) std::cout << "    " << std::hex << std::setw(4) << std::setfill('0') << i << ": ";
            std::cout << std::hex << std::setw(2) << std::setfill('0') << (int)output[i] << " ";
            if ((i + 1) % 16 == 0) std::cout << "\n";
        }
        std::cout << std::dec << "\n";
    }
}

void decompress_16bit_be(const uint8_t* data, size_t size, uint32_t expected_size, const char* label) {
    std::cout << "\n=== " << label << " - 16-bit BE decompression ===\n";

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
        uint16_t code = (data[i] << 8) | data[i+1];
        size_t code_num = i / 2;

        if (code == END_CODE) {
            std::cout << "  END_CODE at code " << code_num << ", output size = " << output.size() << "\n";
            break;
        }

        if (code == CLEAR_CODE) {
            dict_size = FIRST_FREE;
            prev_code = 0xFFFF;
            in_data = true;
            if (code_num < 20 || code_num % 1000 == 0) {
                std::cout << "  CLEAR_CODE at code " << code_num << ", dict reset\n";
            }
            continue;
        }

        if (!in_data) continue;

        if (code_num < 20 || code_num % 1000 == 0) {
            std::cout << "  code[" << code_num << "] = " << code
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
            std::cerr << "  Invalid code " << code << " >= dict_size " << dict_size << " at code_num=" << code_num << "\n";
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

    // Try 12-bit packed decompression
    decompress_12bit_packed(compressed_data2, compressed_size2, decompressed_size2, "inst_12bit_packed");

    // Try 16-bit BE decompression
    decompress_16bit_be(compressed_data2, compressed_size2, decompressed_size2, "inst_16bit_be");

    return 0;
}