#include <vector>
#include <cstdint>
#include <iostream>
#include <fstream>
#include <iomanip>

void trace_bitstream(const uint8_t* data, size_t size, size_t start_bit, int bit_width, int max_codes, const char* label) {
    std::cout << "\n=== " << label << " ===\n";
    size_t bit_pos = start_bit;
    for (int i = 0; i < max_codes && bit_pos + bit_width <= size * 8; i++) {
        uint16_t code = 0;
        for (int b = 0; b < bit_width; b++) {
            size_t byte = bit_pos / 8;
            int bit = bit_pos % 8;
            if (byte >= size) break;
            code |= ((data[byte] >> bit) & 1) << b;
            bit_pos++;
        }
        const char* special = "";
        if (code == 256) special = " <-- CLEAR";
        else if (code == 257) special = " <-- END";
        else if (code < 256) special = " <-- LITERAL";
        std::cout << "  code[" << i << "] = " << code << special << "\n";
    }
}

void find_data_start(const uint8_t* data, size_t size, int bit_width) {
    std::cout << "\n=== Finding data start for bit_width=" << bit_width << " ===\n";
    for (int align = 0; align < 8; align++) {
        size_t bit_pos = align;
        bool found_clear = false;
        size_t clear_pos = 0;
        int clear_idx = 0;
        for (int i = 0; i < 200 && bit_pos + bit_width <= size * 8; i++) {
            uint16_t code = 0;
            for (int b = 0; b < bit_width; b++) {
                size_t byte = bit_pos / 8;
                int bit = bit_pos % 8;
                if (byte >= size) break;
                code |= ((data[byte] >> bit) & 1) << b;
                bit_pos++;
            }
            if (code == 256 && !found_clear) {
                found_clear = true;
                clear_pos = bit_pos;
                clear_idx = i;
            }
            if (found_clear && code < 256) {
                // Check if we have a run of literals after CLEAR
                int literal_run = 0;
                size_t temp_pos = bit_pos;
                for (int j = 0; j < 10 && temp_pos + bit_width <= size * 8; j++) {
                    uint16_t c = 0;
                    for (int b = 0; b < bit_width; b++) {
                        size_t byte = temp_pos / 8;
                        int bit = temp_pos % 8;
                        if (byte >= size) break;
                        c |= ((data[byte] >> bit) & 1) << b;
                        temp_pos++;
                    }
                    if (c < 256) literal_run++;
                    else break;
                }
                if (literal_run >= 3) {
                    std::cout << "  Alignment " << align << ": CLEAR at code " << clear_idx << " (bit_pos=" << (clear_pos - bit_width) << "), "
                              << literal_run << " literals follow\n";
                    break;
                }
            }
        }
        if (!found_clear) {
            std::cout << "  Alignment " << align << ": No CLEAR in first 200 codes\n";
        }
    }
}

int main() {
    // Installed file
    std::ifstream file2("entry_1685", std::ios::binary);
    file2.seekg(0, std::ios::end);
    size_t size2 = file2.tellg();
    file2.seekg(0, std::ios::beg);
    std::vector<uint8_t> data2(size2);
    file2.read(reinterpret_cast<char*>(data2.data()), size2);

    uint32_t compressed_size2 = *reinterpret_cast<uint32_t*>(data2.data() + 0x10);
    const uint8_t* compressed_data2 = data2.data() + 0x20;

    std::cout << "Installed entry_1685: compressed=" << compressed_size2 << "\n";

    find_data_start(compressed_data2, compressed_size2, 9);

    // Trace from promising alignments
    trace_bitstream(compressed_data2, compressed_size2, 1, 9, 30, "Align 1 from start");
    trace_bitstream(compressed_data2, compressed_size2, 55 + 9, 9, 30, "Align 1 after first CLEAR");
    trace_bitstream(compressed_data2, compressed_size2, 61, 9, 30, "Align 5 from start (bit_pos=61)");
    trace_bitstream(compressed_data2, compressed_size2, 13, 9, 30, "Align 13 from start");

    // Also trace 12-bit
    find_data_start(compressed_data2, compressed_size2, 12);

    // Try 12-bit with various alignments
    for (int align = 0; align < 24; align += 3) {
        std::string label = "12-bit align " + std::to_string(align);
        trace_bitstream(compressed_data2, compressed_size2, align, 12, 20, label.c_str());
    }

    // ISO file
    std::ifstream file("entry_0373", std::ios::binary);
    file.seekg(0, std::ios::end);
    size_t size = file.tellg();
    file.seekg(0, std::ios::beg);
    std::vector<uint8_t> data(size);
    file.read(reinterpret_cast<char*>(data.data()), size);

    uint32_t compressed_size = *reinterpret_cast<uint32_t*>(data.data() + 0x10);
    const uint8_t* compressed_data = data.data() + 0x20;

    std::cout << "\n\nISO entry_0373: compressed=" << compressed_size << "\n";
    find_data_start(compressed_data, compressed_size, 9);
    trace_bitstream(compressed_data, compressed_size, 6210, 9, 30, "ISO at first CLEAR (bit_pos=6210)");
    trace_bitstream(compressed_data, compressed_size, 6219, 9, 30, "ISO after first CLEAR (bit_pos=6219)");

    return 0;
}