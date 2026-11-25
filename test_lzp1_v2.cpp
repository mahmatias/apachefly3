#include <vector>
#include <cstdint>
#include <iostream>
#include <fstream>
#include <optional>
#include <bitset>

void analyze_bitstream(const uint8_t* data, size_t size, const char* name) {
    std::cout << "\n=== " << name << " Bitstream Analysis ===\n";
    std::cout << "First 64 bytes of compressed data:\n";
    for (size_t i = 0; i < std::min(size, (size_t)64); i++) {
        if (i % 16 == 0) std::cout << std::hex << std::setw(4) << std::setfill('0') << i << ": ";
        std::cout << std::hex << std::setw(2) << std::setfill('0') << (int)data[i] << " ";
        if ((i + 1) % 16 == 0) std::cout << "\n";
    }
    std::cout << std::dec << "\n";

    // Print first few bits in different orders
    std::cout << "\nFirst 32 bits (LSB-first within byte):\n";
    for (int i = 0; i < 32; i++) {
        size_t byte = i / 8;
        int bit = i % 8;
        int val = (data[byte] >> bit) & 1;
        std::cout << val;
        if ((i + 1) % 8 == 0) std::cout << " ";
    }
    std::cout << "\n";

    std::cout << "First 32 bits (MSB-first within byte):\n";
    for (int i = 0; i < 32; i++) {
        size_t byte = i / 8;
        int bit = 7 - (i % 8);
        int val = (data[byte] >> bit) & 1;
        std::cout << val;
        if ((i + 1) % 8 == 0) std::cout << " ";
    }
    std::cout << "\n";

    // Try reading as 9-bit codes LSB
    std::cout << "\nFirst 10 codes (9-bit LSB):\n";
    size_t bit_pos = 0;
    for (int c = 0; c < 10 && bit_pos + 9 <= size * 8; c++) {
        uint16_t code = 0;
        for (int b = 0; b < 9; b++) {
            size_t byte = bit_pos / 8;
            int bit = bit_pos % 8;
            code |= ((data[byte] >> bit) & 1) << b;
            bit_pos++;
        }
        std::cout << "code[" << c << "] = " << code << "\n";
    }

    // Try reading as 9-bit codes MSB
    std::cout << "\nFirst 10 codes (9-bit MSB):\n";
    bit_pos = 0;
    for (int c = 0; c < 10 && bit_pos + 9 <= size * 8; c++) {
        uint16_t code = 0;
        for (int b = 0; b < 9; b++) {
            size_t byte = bit_pos / 8;
            int bit = 7 - (bit_pos % 8);
            code = (code << 1) | ((data[byte] >> bit) & 1);
            bit_pos++;
        }
        std::cout << "code[" << c << "] = " << code << "\n";
    }

    // Try reading as 12-bit codes LSB
    std::cout << "\nFirst 10 codes (12-bit LSB):\n";
    bit_pos = 0;
    for (int c = 0; c < 10 && bit_pos + 12 <= size * 8; c++) {
        uint16_t code = 0;
        for (int b = 0; b < 12; b++) {
            size_t byte = bit_pos / 8;
            int bit = bit_pos % 8;
            code |= ((data[byte] >> bit) & 1) << b;
            bit_pos++;
        }
        std::cout << "code[" << c << "] = " << code << "\n";
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

    // Header info
    uint32_t compressed_size = *reinterpret_cast<const uint32_t*>(data.data() + 0x10);
    uint32_t decompressed_size = *reinterpret_cast<const uint32_t*>(data.data() + 0x18);
    uint32_t flags = *reinterpret_cast<const uint32_t*>(data.data() + 0x1C);
    const uint8_t* compressed_data = data.data() + 0x20;

    std::cout << "ISO file header:\n";
    std::cout << "  compressed_size: " << compressed_size << " (0x" << std::hex << compressed_size << std::dec << ")\n";
    std::cout << "  decompressed_size: " << decompressed_size << " (0x" << std::hex << decompressed_size << std::dec << ")\n";
    std::cout << "  flags: " << flags << " (0x" << std::hex << flags << std::dec << ")\n";
    std::cout << "  compressed data starts at offset 0x20\n";

    analyze_bitstream(compressed_data, compressed_size, "ISO");

    // Now check installed game file
    std::ifstream file2("entry_1685", std::ios::binary);
    if (file2) {
        file2.seekg(0, std::ios::end);
        size_t size2 = file2.tellg();
        file2.seekg(0, std::ios::beg);
        std::vector<uint8_t> data2(size2);
        file2.read(reinterpret_cast<char*>(data2.data()), size2);

        uint32_t compressed_size2 = *reinterpret_cast<const uint32_t*>(data2.data() + 0x10);
        uint32_t decompressed_size2 = *reinterpret_cast<const uint32_t*>(data2.data() + 0x18);
        uint32_t flags2 = *reinterpret_cast<const uint32_t*>(data2.data() + 0x1C);
        const uint8_t* compressed_data2 = data2.data() + 0x20;

        std::cout << "\n\nInstalled file header:\n";
        std::cout << "  compressed_size: " << compressed_size2 << " (0x" << std::hex << compressed_size2 << std::dec << ")\n";
        std::cout << "  decompressed_size: " << decompressed_size2 << " (0x" << std::hex << decompressed_size2 << std::dec << ")\n";
        std::cout << "  flags: " << flags2 << " (0x" << std::hex << flags2 << std::dec << ")\n";

        analyze_bitstream(compressed_data2, compressed_size2, "Installed");
    }

    return 0;
}