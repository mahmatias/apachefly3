#include <vector>
#include <cstdint>
#include <iostream>
#include <fstream>
#include <iomanip>

void analyze_full_file(const uint8_t* data, size_t size, const char* name) {
    std::cout << "\n=== " << name << " Full File Analysis ===\n";
    std::cout << "Total file size: " << size << "\n";

    // Print header
    std::cout << "\nHeader (first 48 bytes):\n";
    for (size_t i = 0; i < 48; i++) {
        if (i % 16 == 0) std::cout << "  " << std::hex << std::setw(4) << std::setfill('0') << i << ": ";
        std::cout << std::hex << std::setw(2) << std::setfill('0') << (int)data[i] << " ";
        if ((i + 1) % 16 == 0) std::cout << "\n";
    }
    std::cout << std::dec << "\n";

    uint32_t compressed_size = *reinterpret_cast<const uint32_t*>(data + 0x10);
    uint32_t decompressed_size = *reinterpret_cast<const uint32_t*>(data + 0x18);
    uint32_t flags = *reinterpret_cast<const uint32_t*>(data + 0x1C);

    std::cout << "Header fields:\n";
    std::cout << "  0x10: compressed_size = " << compressed_size << " (0x" << std::hex << compressed_size << std::dec << ")\n";
    std::cout << "  0x14: magic = " << std::string((char*)data+0x14, 4) << "\n";
    std::cout << "  0x18: decompressed_size = " << decompressed_size << " (0x" << std::hex << decompressed_size << std::dec << ")\n";
    std::cout << "  0x1C: flags = " << flags << " (0x" << std::hex << flags << std::dec << ")\n";

    // Check data after 0x20
    std::cout << "\nData at 0x20 (first 64 bytes):\n";
    for (size_t i = 0; i < std::min(size - 0x20, (size_t)64); i++) {
        if (i % 16 == 0) std::cout << "  " << std::hex << std::setw(4) << std::setfill('0') << i << ": ";
        std::cout << std::hex << std::setw(2) << std::setfill('0') << (int)data[0x20 + i] << " ";
        if ((i + 1) % 16 == 0) std::cout << "\n";
    }
    std::cout << std::dec << "\n";

    // Look for CLEAR (256=0x0100) and END (257=0x0101) as 16-bit values in the data
    std::cout << "\nSearching for 0x0100 (CLEAR) and 0x0101 (END) as 16-bit LE:\n";
    for (size_t i = 0x20; i + 1 < size; i++) {
        uint16_t val = data[i] | (data[i+1] << 8);
        if (val == 0x0100 || val == 0x0101) {
            std::cout << "  Offset 0x" << std::hex << i << std::dec << ": " << (val == 0x0100 ? "CLEAR" : "END") << "\n";
        }
    }

    // Also check if compressed_size might be at a different offset
    std::cout << "\nChecking other 32-bit values in header that could be sizes:\n";
    for (int offset = 0; offset < 32; offset += 4) {
        uint32_t val = *reinterpret_cast<const uint32_t*>(data + offset);
        std::cout << "  Offset 0x" << std::hex << offset << std::dec << ": " << val << " (0x" << std::hex << val << std::dec << ")\n";
    }

    // Check if the "extra" data after compressed_size might be another resource
    if (size > 0x20 + compressed_size) {
        size_t extra_start = 0x20 + compressed_size;
        size_t extra_size = size - extra_start;
        std::cout << "\nExtra data after declared compressed_size (" << compressed_size << "):\n";
        std::cout << "  Starts at offset 0x" << std::hex << extra_start << std::dec << ", size = " << extra_size << "\n";
        std::cout << "  First 64 bytes:\n";
        for (size_t i = 0; i < std::min(extra_size, (size_t)64); i++) {
            if (i % 16 == 0) std::cout << "  " << std::hex << std::setw(4) << std::setfill('0') << i << ": ";
            std::cout << std::hex << std::setw(2) << std::setfill('0') << (int)data[extra_start + i] << " ";
            if ((i + 1) % 16 == 0) std::cout << "\n";
        }
        std::cout << std::dec << "\n";

        // Check if extra data has LZP1 header
        if (extra_size >= 32) {
            const char* magic = reinterpret_cast<const char*>(data + extra_start + 0x14);
            if (magic[0] == 'L' && magic[1] == 'Z' && magic[2] == 'P' && magic[3] == '1') {
                std::cout << "  *** Extra data also has LZP1 header! ***\n";
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
    analyze_full_file(data.data(), size, "ISO entry_0373");

    // Installed file
    std::ifstream file2("entry_1685", std::ios::binary);
    file2.seekg(0, std::ios::end);
    size_t size2 = file2.tellg();
    file2.seekg(0, std::ios::beg);
    std::vector<uint8_t> data2(size2);
    file2.read(reinterpret_cast<char*>(data2.data()), size2);
    analyze_full_file(data2.data(), size2, "Installed entry_1685");

    return 0;
}