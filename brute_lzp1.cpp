#include <fstream>
#include <iostream>
#include <vector>
#include <cstdint>
#include <string>

using namespace std;

static uint16_t read_bits_lsb(const uint8_t* data, size_t& pos, size_t max_bits, int n) {
    uint16_t val = 0;
    for (int b = 0; b < n; b++) {
        if (pos + b >= max_bits) break;
        size_t byte = (pos + b) / 8;
        int bit = (pos + b) % 8;
        val |= ((data[byte] >> bit) & 1) << b;
    }
    pos += n;
    return val;
}

static uint16_t read_bits_msb(const uint8_t* data, size_t& pos, size_t max_bits, int n) {
    uint16_t val = 0;
    for (int b = 0; b < n; b++) {
        if (pos + b >= max_bits) break;
        size_t byte = (pos + b) / 8;
        int bit = 7 - ((pos + b) % 8);
        val |= ((data[byte] >> bit) & 1) << b;
    }
    pos += n;
    return val;
}

struct Result { bool ok; size_t out_size; size_t end_pos; };

static Result try_decode(const uint8_t* cdata, size_t max_bits, size_t start,
                         bool msb, int start_width, size_t max_out, bool require_clear) {
    using R = uint16_t(*)(const uint8_t*, size_t&, size_t, int);
    R rfn = msb ? read_bits_msb : read_bits_lsb;
    vector<uint8_t> output;
    vector<uint16_t> prefix(65536, 0);
    vector<uint8_t> ch(65536, 0);
    for (uint16_t i = 0; i < 256; ++i) { prefix[i] = 0xFFFF; ch[i] = (uint8_t)i; }

    const uint16_t CLEAR = 256, END = 257, FIRST_FREE = 258;

    size_t pos = start;
    int bit_width = start_width;
    uint16_t max_code = (1 << bit_width) - 1;
    uint16_t prev_code = 0xFFFF;
    uint16_t dict_size = FIRST_FREE;
    bool seen_clear = false;

    while (pos < max_bits && output.size() < max_out) {
        uint16_t code = rfn(cdata, pos, max_bits, bit_width);
        if (code == END) return {true, output.size(), pos};
        if (code == CLEAR) { seen_clear = true; dict_size = FIRST_FREE; bit_width = 9; max_code = 511; prev_code = 0xFFFF; continue; }
        if (require_clear && !seen_clear) return {false, 0, 0};
        if (prev_code == 0xFFFF) { if (code < 256) { output.push_back((uint8_t)code); prev_code = code; } else return {false,0,0}; continue; }
        if (code > dict_size) return {false, 0, 0};
        uint16_t cur = code;
        bool append_first = (code == dict_size);
        if (code == dict_size) cur = prev_code;
        vector<uint8_t> stack;
        while (cur >= 256) { stack.push_back(ch[cur]); cur = prefix[cur]; }
        stack.push_back((uint8_t)cur);
        for (auto it = stack.rbegin(); it != stack.rend(); ++it) output.push_back(*it);
        if (append_first) output.push_back((uint8_t)cur);
        if (dict_size < 65536) {
            prefix[dict_size] = prev_code;
            ch[dict_size] = append_first ? (uint8_t)cur : ch[code];
            dict_size++;
        }
        if (dict_size > max_code && bit_width < 16) { bit_width++; max_code = (1 << bit_width) - 1; }
        prev_code = code;
    }
    return {false, output.size(), pos};
}

int main() {
    ifstream in("entry_1685", ios::binary);
    vector<uint8_t> data((istreambuf_iterator<char>(in)), istreambuf_iterator<char>());
    uint32_t compressed_size = *reinterpret_cast<uint32_t*>(data.data() + 0x10);
    uint32_t decompressed_size = *reinterpret_cast<uint32_t*>(data.data() + 0x18);
    const uint8_t* cdata = data.data() + 0x20;
    size_t max_bits = compressed_size * 8;

    cout << "compressed_size=" << compressed_size << " decompressed_size=" << decompressed_size << "\n";

    for (int xor_key = 0; xor_key < 256; xor_key++) {
        vector<uint8_t> xdata(compressed_size);
        for (size_t i = 0; i < compressed_size; i++) xdata[i] = cdata[i] ^ (uint8_t)xor_key;

        for (int msb = 0; msb < 2; msb++) {
            for (int sw = 9; sw <= 12; sw++) {
                auto r = try_decode(xdata.data(), max_bits, 0, msb != 0, sw, 200000, false);
                if (r.ok && r.out_size > 500) {
                    cout << "XOR=0x" << hex << xor_key << dec
                         << " msb=" << msb << " width=" << sw
                         << " OK output=" << r.out_size << " endpos=" << r.end_pos << "\n";
                    // Print first 32 bytes
                    cout << "  First 32: ";
                    for (size_t i = 0; i < min(r.out_size, (size_t)32); i++) {
                        // Need to re-decompress to get bytes... skip for now
                    }
                    cout << "\n";
                }
            }
        }
    }
    cout << "Done\n";
    return 0;
}
