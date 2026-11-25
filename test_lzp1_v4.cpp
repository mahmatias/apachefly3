#include <vector>
#include <cstdint>
#include <iostream>
#include <fstream>
#include <iomanip>

int main() {
    // Check ISO file structure
    std::ifstream file("entry_0373", std::ios::binary);
    file.seekg(0, std::ios::end);
    size_t size = file.tellg();
    file.seekg(0, std::ios::beg);
    std::vector<uint8_t> data(size);
    file.read(reinterpret_cast<char*>(data.data()), size);

    uint32_t compressed_size = *reinterpret_cast<uint32_t*>(data.data() + 0x10);
    uint32_t decompressed_size = *reinterpret_cast<uint32_t*>(data.data() + 0x18);

    std::cout << "ISO entry_0373:\n";
    std::cout << "  File size: " << size << "\n";
    std::cout << "  Header compressed_size: " << compressed_size << "\n";
    std::cout << "  Header decompressed_size: " << decompressed_size << "\n";
    std::cout << "  Data after header (0x20): " << (size - 0x20) << " bytes\n";
    std::cout << "  Expected compressed data size: " << compressed_size << "\n";
    std::cout << "  Match: " << ((size - 0x20) == compressed_size ? "YES" : "NO") << "\n\n";

    // Check installed file structure
    std::ifstream file2("entry_1685", std::ios::binary);
    file2.seekg(0, std::ios::end);
    size_t size2 = file2.tellg();
    file2.seekg(0, std::ios::beg);
    std::vector<uint8_t> data2(size2);
    file2.read(reinterpret_cast<char*>(data2.data()), size2);

    uint32_t compressed_size2 = *reinterpret_cast<uint32_t*>(data2.data() + 0x10);
    uint32_t decompressed_size2 = *reinterpret_cast<uint32_t*>(data2.data() + 0x18);

    std::cout << "Installed entry_1685:\n";
    std::cout << "  File size: " << size2 << "\n";
    std::cout << "  Header compressed_size: " << compressed_size2 << "\n";
    std::cout << "  Header decompressed_size: " << decompressed_size2 << "\n";
    std::cout << "  Data after header (0x20): " << (size2 - 0x20) << " bytes\n";
    std::cout << "  Expected compressed data size: " << compressed_size2 << "\n";
    std::cout << "  Match: " << ((size2 - 0x20) == compressed_size2 ? "YES" : "NO") << "\n\n";

    // Look at data after compressed section in installed file
    if (size2 > 0x20 + compressed_size2) {
        size_t extra_start = 0x20 + compressed_size2;
        size_t extra_size = size2 - extra_start;
        std::cout << "  Extra data after compressed section: " << extra_size << " bytes at offset 0x" << std::hex << extra_start << std::dec << "\n";
        std::cout << "  First 64 bytes of extra data:\n";
        for (size_t i = 0; i < std::min(extra_size, (size_t)64); i++) {
            if (i % 16 == 0) std::cout << "    " << std::hex << std::setw(4) << std::setfill('0') << i << ": ";
            std::cout << std::hex << std::setw(2) << std::setfill('0') << (int)data2[extra_start + i] << " ";
            if ((i + 1) % 16 == 0) std::cout << "\n";
        }
        std::cout << std::dec << "\n";
    }

    // Check if the "compressed" data in installed file is actually valid LZW
    // by looking for CLEAR_CODE (256) and END_CODE (257) patterns in the bitstream
    const uint8_t* comp_data2 = data2.data() + 0x20;
    std::cout << "\n  Searching for CLEAR_CODE (256) and END_CODE (257) in installed bitstream:\n";

    // Try 9-bit LSB
    size_t bit_pos = 0;
    for (int i = 0; i < 1000 && bit_pos + 9 <= compressed_size2 * 8; i++) {
        uint16_t code = 0;
        for (int b = 0; b < 9; b++) {
            size_t byte = bit_pos / 8;
            int bit = bit_pos % 8;
            code |= ((comp_data2[byte] >> bit) & 1) << b;
            bit_pos++;
        }
        if (code == 256 || code == 257) {
            std::cout << "    9-bit LSB: code[" << i << "] = " << code << " (bit_pos=" << bit_pos << ")\n";
        }
    }

    // Try 12-bit LSB at various offsets
    for (int offset = 0; offset < 32; offset += 4) {
        bit_pos = offset;
        bool found = false;
        for (int i = 0; i < 100 && bit_pos + 12 <= compressed_size2 * 8; i++) {
            uint16_t code = 0;
            for (int b = 0; b < 12; b++) {
                size_t byte = bit_pos / 8;
                int bit = bit_pos % 8;
                if (byte >= compressed_size2) break;
                code |= ((comp_data2[byte] >> bit) & 1) << b;
                bit_pos++;
            }
            if (code == 256 || code == 257) {
                std::cout << "    12-bit LSB offset " << offset << ": code[" << i << "] = " << code << "\n";
                found = true;
                break;
            }
        }
        if (!found) {
            std::cout << "    12-bit LSB offset " << offset << ": no CLEAR/END in first 100 codes\n";
        }
    }

    return 0;
}