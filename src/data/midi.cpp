#include "data/midi.hpp"

#include <cstring>
#include <iostream>
#include <sstream>

namespace c3 {

namespace {

uint16_t read_be16(const uint8_t* p) {
    return static_cast<uint16_t>((static_cast<uint16_t>(p[0]) << 8) |
                                 static_cast<uint16_t>(p[1]));
}

uint32_t read_be32(const uint8_t* p) {
    return (static_cast<uint32_t>(p[0]) << 24) |
           (static_cast<uint32_t>(p[1]) << 16) |
           (static_cast<uint32_t>(p[2]) << 8) |
           static_cast<uint32_t>(p[3]);
}

// Read a variable-length quantity (MIDI delta time).
// Advances pos past the VLQ. Returns false if data runs out.
bool read_vlq(const uint8_t* data, size_t& pos, size_t size, uint32_t& value) {
    value = 0;
    for (int i = 0; i < 4; ++i) {
        if (pos >= size) return false;
        uint8_t b = data[pos++];
        value = (value << 7) | (b & 0x7F);
        if ((b & 0x80) == 0) return true;
    }
    return true;
}

} // namespace

std::optional<MidiFile> decode_midi(const std::vector<uint8_t>& data) {
    if (data.size() < 14) {
        std::cerr << "MIDI: file too small\n";
        return std::nullopt;
    }

    // MThd header chunk
    if (data[0] != 'M' || data[1] != 'T' || data[2] != 'h' || data[3] != 'd') {
        std::cerr << "MIDI: missing MThd magic\n";
        return std::nullopt;
    }

    uint32_t header_size = read_be32(data.data() + 4);
    if (header_size < 6) {
        std::cerr << "MIDI: invalid header size\n";
        return std::nullopt;
    }

    MidiFile midi;
    midi.format = read_be16(data.data() + 8);
    midi.ntracks = read_be16(data.data() + 10);
    midi.division = read_be16(data.data() + 12);

    size_t pos = 14 + (header_size - 6);

    for (uint16_t t = 0; t < midi.ntracks; ++t) {
        if (pos + 8 > data.size()) break;

        // MTrk chunk
        if (data[pos] != 'M' || data[pos+1] != 'T' || data[pos+2] != 'r' ||
            data[pos+3] != 'k') {
            std::cerr << "MIDI: missing MTrk magic at track " << t << "\n";
            break;
        }
        uint32_t track_size = read_be32(data.data() + pos + 4);
        pos += 8;

        size_t track_end = pos + track_size;
        if (track_end > data.size()) track_end = data.size();

        MidiTrack track;
        while (pos < track_end) {
            uint32_t delta = 0;
            if (!read_vlq(data.data(), pos, track_end, delta)) break;

            if (pos >= track_end) break;
            uint8_t status = data[pos++];

            // Running status: if the byte is a data value (< 0x80), reuse
            // the previous status byte.
            if ((status & 0x80) == 0) {
                // running status case — but we don't track prev status here,
                // so just treat as malformed
                std::cerr << "MIDI: running status not supported\n";
                break;
            }

            // Meta events (0xFF) and sysex (0xF0) have variable length.
            if (status == 0xFF) {
                if (pos >= track_end) break;
                uint8_t meta_type = data[pos++];
                uint32_t meta_len = 0;
                if (!read_vlq(data.data(), pos, track_end, meta_len)) break;
                pos += meta_len; // skip meta payload
                continue;
            } else if (status == 0xF0 || status == 0xF7) {
                uint32_t sysex_len = 0;
                if (!read_vlq(data.data(), pos, track_end, sysex_len)) break;
                pos += sysex_len;
                continue;
            }

            // Channel voice / mode messages: 1 or 2 data bytes depending on status.
            uint8_t data1 = 0, data2 = 0;
            uint8_t cmd = status & 0xF0;
            if (cmd >= 0x80 && cmd <= 0xB0) {
                if (pos >= track_end) break;
                data1 = data[pos++];
                if (cmd != 0xC0 && cmd != 0xD0) {
                    if (pos >= track_end) break;
                    data2 = data[pos++];
                }
            } else {
                // System common / realtime — skip 1-2 bytes heuristically
                if (pos >= track_end) break;
                data1 = data[pos++];
            }

            track.events.push_back({delta, status, data1, data2});
        }

        midi.tracks.push_back(std::move(track));
        pos = track_end;
    }

    return midi;
}

} // namespace c3
