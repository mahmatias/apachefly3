#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <optional>

namespace c3 {

struct MidiEvent {
    uint32_t delta_time;
    uint8_t status;
    uint8_t data1;
    uint8_t data2;
};

struct MidiTrack {
    std::vector<MidiEvent> events;
};

struct MidiFile {
    uint16_t format = 0;
    uint16_t ntracks = 0;
    uint16_t division = 0;
    std::vector<MidiTrack> tracks;
};

std::optional<MidiFile> decode_midi(const std::vector<uint8_t>& data);

} // namespace c3
