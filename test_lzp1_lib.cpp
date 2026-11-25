#include "data/lzp1.hpp"
#include "data/resource2.hpp"
#include <iostream>
#include <fstream>
#include <vector>

int main() {
    // Test with installed game
    c3::ResourceIndex index = c3::ResourceIndex::load_from_disk([&]() {
        std::cout << "Loading from disk: D:/Games/C3" << std::endl;
        return std::filesystem::path("D:/Games/C3");
    }());

    std::cout << "Shards found: " << index.shard_paths().size() << std::endl;
    std::cout << "Total entries: " << index.entries().size() << std::endl;

    // Find entry_1685
    auto opt = index.find("entry_1685");
    if (!opt) {
        std::cerr << "Resource not found: entry_1685" << std::endl;
        return 1;
    }

    auto data = index.read_resource(*opt);
    if (data.empty()) {
        std::cerr << "Failed to read resource" << std::endl;
        return 1;
    }

    std::cout << "Read resource: " << data.size() << " bytes" << std::endl;

    // Try decompression using library function
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