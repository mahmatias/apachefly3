#include <fstream>
#include <iostream>
#include <vector>
#include <cstdint>

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

static uint8_t reverse_bits(uint8_t b) {
    uint8_t r = 0;
    for (int i = 0; i < 8; i++) r = (r << 1) | ((b >> i) & 1);
    return r;
}

struct Result { bool ok; size_t out_size; size_t end_pos; vector<uint8_t> output; };

static Result try_decode(const uint8_t* cdata, size_t max_bits, size_t start,
                         bool msb, int start_width, size_t max_out) {
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

    while (pos < max_bits && output.size() < max_out) {
        uint16_t code = rfn(cdata, pos, max_bits, bit_width);
        if (code == END) return {true, output.size(), pos, output};
        if (code == CLEAR) { dict_size = FIRST_FREE; bit_width = 9; max_code = 511; prev_code = 0xFFFF; continue; }
        if (prev_code == 0xFFFF) { if (code < 256) { output.push_back((uint8_t)code); prev_code = code; } else return {false,0,0,output}; continue; }
        if (code > dict_size) return {false, output.size(), pos, output};
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
    return {false, output.size(), pos, output};
}

int main() {
    ifstream in("entry_1685", ios::binary);
    vector<uint8_t> data((istreambuf_iterator<char>(in)), istreambuf_iterator<char>());
    uint32_t compressed_size = *reinterpret_cast<uint32_t*>(data.data() + 0x10);
    const uint8_t* cdata = data.data() + 0x20;
    size_t max_bits = compressed_size * 8;

    cout << "compressed_size=" << compressed_size << "\n";

    int total_tested = 0;
    int total_partial = 0;

    for (int xor_key = 0; xor_key < 256; xor_key++) {
        for (int rev = 0; rev < 2; rev++) {
            vector<uint8_t> xdata(compressed_size);
            for (size_t i = 0; i < compressed_size; i++) {
                uint8_t b = cdata[i] ^ (uint8_t)xor_key;
                xdata[i] = rev ? reverse_bits(b) : b;
            }

            for (int msb = 0; msb < 2; msb++) {
                for (int sw = 8; sw <= 14; sw++) {
                    for (int start = 0; start < 8; start++) {
                        total_tested++;
                        auto r = try_decode(xdata.data(), max_bits, start, msb != 0, sw, 100000);
                        if (r.ok && r.out_size > 1000) {
                            cout << "MATCH: XOR=0x" << hex << xor_key << dec
                                 << " rev=" << rev << " msb=" << msb << " width=" << sw
                                 << " start=" << start
                                 << " OK output=" << r.out_size << " endpos=" << r.end_pos << "\n";
                            cout << "  First 64 bytes: ";
                            for (size_t i = 0; i < min(r.out_size, (size_t)64); i++) cout << r.output[i];
                            cout << "\n";
                            return 0;
                        }
                        if (r.out_size > 500 && !r.ok) {
                            total_partial++;
                            if (total_partial <= 5) {
                                cout << "PARTIAL: XOR=0x" << hex << xor_key << dec
                                     << " rev=" << rev << " msb=" << msb << " width=" << sw
                                     << " start=" << start
                                     << " output=" << r.out_size << "\n";
                            }
                        }
                    }
                }
            }
        }
    }
    cout << "Done. Tested=" << total_tested << " Partials=" << total_partial << "\n";
    return 0;
}
