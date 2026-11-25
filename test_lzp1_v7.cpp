#include <vector>
#include <cstdint>
#include <iostream>
#include <fstream>
#include <iomanip>

void find_clear_end_codes(const uint8_t* data, size_t size, const char* name) {
    std::cout << "\n=== " << name << " - Searching for CLEAR(256) and END(257) codes ===\n";

    for (int bit_width = 9; bit_width <= 13; bit_width++) {
        std::cout << "\n  Bit width " << bit_width << " (LSB):\n";
        size_t bit_pos = 0;
        for (int i = 0; i < 5000 && bit_pos + bit_width <= size * 8; i++) {
            uint16_t code = 0;
            for (int b = 0; b < bit_width; b++) {
                size_t byte = bit_pos / 8;
                int bit = bit_pos % 8;
                if (byte >= size) break;
                code |= ((data[byte] >> bit) & 1) << b;
                bit_pos++;
            }
            if (code == 256 || code == 257) {
                std::cout << "    code[" << i << "] = " << code << " at bit_pos=" << bit_pos << " (byte=" << (bit_pos/8) << ")\n";
            }
        }

        std::cout << "  Bit width " << bit_width << " (MSB):\n";
        bit_pos = 0;
        for (int i = 0; i < 5000 && bit_pos + bit_width <= size * 8; i++) {
            uint16_t code = 0;
            for (int b = 0; b < bit_width; b++) {
                size_t byte = bit_pos / 8;
                int bit = 7 - (bit_pos % 8);
                if (byte >= size) break;
                code = (code << 1) | ((data[byte] >> bit) & 1);
                bit_pos++;
            }
            if (code == 256 || code == 257) {
                std::cout << "    code[" << i << "] = " << code << " at bit_pos=" << bit_pos << " (byte=" << (bit_pos/8) << ")\n";
            }
        }
    }
}

void check_specific_offsets(const uint8_t* data, size_t size) {
    std::cout << "\n=== Checking specific byte offsets for CLEAR/END ===\n";
    // Check if CLEAR/END appear as byte values in the stream
    for (size_t i = 0; i < size; i++) {
        if (data[i] == 0x00 || data[i] == 0x01) {  // Could be CLEAR=256 (0x0100) or END=257 (0x0101) in little-endian 16-bit
            // Check next byte
            if (i + 1 < size) {
                uint16_t val = data[i] | (data[i+1] << 8);
                if (val == 256 || val == 257) {
                    std::cout << "  16-bit LE at offset " << i << ": " << val << "\n";
                }
                val = (data[i] << 8) | data[i+1];
                if (val == 256 || val == 257) {
                    std::cout << "  16-bit BE at offset " << i << ": " << val << "\n";
                }
            }
        }
    }
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
    const uint8_t* compressed_data = data.data() + 0x20;

    std::cout << "ISO entry_0373: compressed_size=" << compressed_size << "\n";
    find_clear_end_codes(compressed_data, compressed_size, "ISO");
    check_specific_offsets(compressed_data, compressed_size);

    // Installed file
    std::ifstream file2("entry_1685", std::ios::binary);
    file2.seekg(0, std::ios::end);
    size_t size2 = file2.tellg();
    file2.seekg(0, std::ios::beg);
    std::vector<uint8_t> data2(size2);
    file2.read(reinterpret_cast<char*>(data2.data()), size2);

    uint32_t compressed_size2 = *reinterpret_cast<uint32_t*>(data2.data() + 0x10);
    const uint8_t* compressed_data2 = data2.data() + 0x20;

    std::cout << "\n\nInstalled entry_1685: compressed_size=" << compressed_size2 << "\n";
    find_clear_end_codes(compressed_data2, compressed_size2, "Installed");
    check_specific_offsets(compressed_data2, compressed_size2);

    return 0;
}