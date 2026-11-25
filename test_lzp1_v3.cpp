#include <vector>
#include <cstdint>
#include <iostream>
#include <fstream>
#include <optional>
#include <iomanip>

void try_all_bitwidths(const uint8_t* data, size_t size, const char* name) {
    std::cout << "\n=== " << name << " - Trying different bit widths ===\n";

    for (int bit_width = 8; bit_width <= 16; bit_width++) {
        std::cout << "\n--- Bit width: " << bit_width << " ---\n";

        // LSB-first
        size_t bit_pos = 0;
        std::cout << "LSB-first first 15 codes:\n";
        for (int c = 0; c < 15 && bit_pos + bit_width <= size * 8; c++) {
            uint16_t code = 0;
            for (int b = 0; b < bit_width; b++) {
                size_t byte = bit_pos / 8;
                int bit = bit_pos % 8;
                if (byte >= size) break;
                code |= ((data[byte] >> bit) & 1) << b;
                bit_pos++;
            }
            std::cout << "  code[" << c << "] = " << code << "\n";
        }

        // MSB-first
        bit_pos = 0;
        std::cout << "MSB-first first 15 codes:\n";
        for (int c = 0; c < 15 && bit_pos + bit_width <= size * 8; c++) {
            uint16_t code = 0;
            for (int b = 0; b < bit_width; b++) {
                size_t byte = bit_pos / 8;
                int bit = 7 - (bit_pos % 8);
                if (byte >= size) break;
                code = (code << 1) | ((data[byte] >> bit) & 1);
                bit_pos++;
            }
            std::cout << "  code[" << c << "] = " << code << "\n";
        }
    }
}

void try_byte_aligned(const uint8_t* data, size_t size, const char* name) {
    std::cout << "\n=== " << name << " - Byte-aligned codes ===\n";

    for (int bytes_per_code = 1; bytes_per_code <= 3; bytes_per_code++) {
        std::cout << "\n--- " << bytes_per_code << " byte(s) per code ---\n";
        for (int c = 0; c < 20 && c * bytes_per_code < (int)size; c++) {
            uint32_t code = 0;
            for (int b = 0; b < bytes_per_code; b++) {
                code |= (data[c * bytes_per_code + b] << (b * 8));
            }
            std::cout << "  code[" << c << "] = " << code << " (0x" << std::hex << code << std::dec << ")\n";
        }
    }
}

void try_with_offset(const uint8_t* data, size_t size, int start_bit_offset) {
    std::cout << "\n=== With bit offset " << start_bit_offset << " ===\n";
    for (int bit_width = 9; bit_width <= 13; bit_width++) {
        size_t bit_pos = start_bit_offset;
        std::cout << "Bit width " << bit_width << " (LSB):\n";
        for (int c = 0; c < 10 && bit_pos + bit_width <= size * 8; c++) {
            uint16_t code = 0;
            for (int b = 0; b < bit_width; b++) {
                size_t byte = bit_pos / 8;
                int bit = bit_pos % 8;
                if (byte >= size) break;
                code |= ((data[byte] >> bit) & 1) << b;
                bit_pos++;
            }
            std::cout << "  code[" << c << "] = " << code << "\n";
        }
    }
}

int main() {
    std::ifstream file("entry_0373", std::ios::binary);
    if (!file) {
        std::cerr << "Failed to open entry_0373\n";
        return 1;
    }
    file.seekg(0, std::ios::end);
    size_t size = file.tellg();
    file.seekg(0, std::ios::beg);
    std::vector<uint8_t> data(size);
    file.read(reinterpret_cast<char*>(data.data()), size);

    uint32_t compressed_size = *reinterpret_cast<const uint32_t*>(data.data() + 0x10);
    const uint8_t* compressed_data = data.data() + 0x20;

    std::cout << "ISO file - compressed_size: " << compressed_size << "\n";

    try_all_bitwidths(compressed_data, compressed_size, "ISO");
    try_byte_aligned(compressed_data, compressed_size, "ISO");

    // Try with various bit offsets
    for (int offset = 0; offset < 32; offset += 4) {
        try_with_offset(compressed_data, compressed_size, offset);
    }

    // Also check installed file
    std::ifstream file2("entry_1685", std::ios::binary);
    if (file2) {
        file2.seekg(0, std::ios::end);
        size_t size2 = file2.tellg();
        file2.seekg(0, std::ios::beg);
        std::vector<uint8_t> data2(size2);
        file2.read(reinterpret_cast<char*>(data2.data()), size2);

        uint32_t compressed_size2 = *reinterpret_cast<const uint32_t*>(data2.data() + 0x10);
        const uint8_t* compressed_data2 = data2.data() + 0x20;

        std::cout << "\n\n========================================\n";
        std::cout << "INSTALLED FILE - compressed_size: " << compressed_size2 << "\n";

        try_all_bitwidths(compressed_data2, compressed_size2, "Installed");
        try_byte_aligned(compressed_data2, compressed_size2, "Installed");
    }

    return 0;
}