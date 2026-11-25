#include "data/lzp1.hpp"
#include "data/resource2.hpp"
#include <iostream>
#include <fstream>
#include <vector>
#include <iomanip>

int main(int argc, char* argv[]) {
    std::string test_file = "entry_0375";
    if (argc > 1) test_file = argv[1];

    // Read file directly
    std::ifstream in(test_file, std::ios::binary);
    std::vector<uint8_t> data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());

    std::cout << "Read file: " << test_file << " (" << data.size() << " bytes)" << std::endl;

    // Parse LZP1 header
    if (data.size() < 0x20) {
        std::cerr << "File too small for LZP1 header" << std::endl;
        return 1;
    }

    uint32_t compressed_size = *reinterpret_cast<const uint32_t*>(data.data() + 0x10);
    uint32_t decompressed_size = *reinterpret_cast<const uint32_t*>(data.data() + 0x18);
    uint32_t flags = *reinterpret_cast<const uint32_t*>(data.data() + 0x1C);

    std::cout << "Header: comp_size=" << compressed_size
              << " decomp_size=" << decompressed_size
              << " flags=" << flags << std::endl;

    // Extract compressed data (skip 32-byte header)
    const uint8_t* comp_data = data.data() + 0x20;
    size_t comp_len = std::min<size_t>(compressed_size, data.size() - 0x20);

    std::cout << "Read resource: " << comp_len << " bytes (compressed)" << std::endl;

    // Try decompression using library function (pass full file with header)
    auto decompressed = c3::data::lzp1_decompress(data.data(), data.size());
    if (decompressed) {
        std::cout << "Decompressed size: " << decompressed->size() << std::endl;
        std::ofstream out("decompressed_lib.bin", std::ios::binary);
        out.write(reinterpret_cast<const char*>(decompressed->data()), decompressed->size());
        std::cout << "Written to decompressed_lib.bin" << std::endl;

        // Check first 32 bytes
        std::cout << "First 32 bytes:\n";
        for (size_t i = 0; i < std::min(decompressed->size(), (size_t)32); i++) {
            if (i % 16 == 0) std::cout << "  " << std::hex << std::setw(4) << std::setfill('0') << i << ": ";
            std::cout << std::hex << std::setw(2) << std::setfill('0') << (int)(*decompressed)[i] << " ";
            if ((i + 1) % 16 == 0) std::cout << "\n";
        }
        std::cout << std::dec << "\n";
    } else {
        std::cerr << "Decompression failed!" << std::endl;
    }

    return 0;
}