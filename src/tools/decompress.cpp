#include "data/lzp1.hpp"
#include <iostream>
#include <fstream>
#include <vector>
#include <filesystem>
#include <iomanip>

namespace fs = std::filesystem;

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <input_file> [output_file]\n";
        return 1;
    }

    std::string input_path = argv[1];
    std::string output_path = (argc >= 3) ? argv[2] : (input_path + ".dec");

    std::ifstream in(input_path, std::ios::binary);
    if (!in) {
        std::cerr << "Failed to open input: " << input_path << std::endl;
        return 1;
    }

    in.seekg(0, std::ios::end);
    size_t size = in.tellg();
    in.seekg(0, std::ios::beg);

    std::vector<uint8_t> data(size);
    in.read(reinterpret_cast<char*>(data.data()), size);

    auto result = c3::data::lzp1_decompress(data.data(), size);
    if (!result) {
        std::cerr << "LZP1 decompression failed\n";
        // Debug: print header info
        if (size >= 32) {
            uint32_t compressed_size = *reinterpret_cast<const uint32_t*>(data.data() + 0x10);
            uint32_t decompressed_size = *reinterpret_cast<const uint32_t*>(data.data() + 0x18);
            uint32_t flags = *reinterpret_cast<const uint32_t*>(data.data() + 0x1C);
            std::cerr << "  compressed_size: " << compressed_size << " (0x" << std::hex << compressed_size << std::dec << ")\n";
            std::cerr << "  decompressed_size: " << decompressed_size << " (0x" << std::hex << decompressed_size << std::dec << ")\n";
            std::cerr << "  flags: 0x" << std::hex << flags << std::dec << "\n";
            std::cerr << "  src_size: " << size << "\n";
            // Print first 64 bytes of compressed data
            std::cerr << "  compressed data (first 64 bytes):\n";
            for (int i = 0; i < 64 && i < size - 32; ++i) {
                std::cerr << std::hex << std::setw(2) << std::setfill('0') << (int)data.data()[32 + i] << " ";
                if ((i + 1) % 16 == 0) std::cerr << "\n";
            }
            std::cerr << std::dec << "\n";
        }
        return 1;
    }

    std::ofstream out(output_path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(result->data()), result->size());

    std::cout << "Decompressed: " << result->size() << " bytes -> " << output_path << std::endl;

    // Check for known magic
    if (result->size() >= 4) {
        uint32_t magic = *reinterpret_cast<uint32_t*>(result->data());
        if (magic == 0x314F4433) {
            std::cout << "  -> 3DO1 model detected!\n";
        } else if (result->data()[0] == 0x0A) {
            std::cout << "  -> PCX image detected!\n";
        } else if (result->size() >= 4 && result->data()[0] == 'M' && result->data()[1] == 'T' && result->data()[2] == 'h' && result->data()[3] == 'd') {
            std::cout << "  -> MIDI detected!\n";
        } else {
            std::cout << "  -> Magic: 0x" << std::hex << magic << std::dec << "\n";
        }
    }

    return 0;
}